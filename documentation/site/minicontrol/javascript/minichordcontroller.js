// MiniChord Controller - Handles MIDI communication
class MiniChordController {
    constructor() {
      this.device = false;
      this.parameter_size = 256;
      this.color_hue_sysex_adress = 20;
      this.base_adress_rythm = 220;
      this.potentiometer_memory_adress = [4, 5, 6];
      this.modulation_adress = [14, 10, 12,16];
      this.volume_memory_adress = [2, 3];
      this.active_bank_number = -1;
      this.min_firmware_accepted = 0.02;
      this.firmware_adress = 7;
      this.float_multiplier = 100.0;
      this.MIDI_request_option = {
        sysex: true,
        software: false
      };
      this.onConnectionChange = null;
      this.onDataReceived = null;
      this.json_reference="../json/minichord.json";
    }
  
    // Initialize MIDI connection
    async initialize() {
      try {
        console.log(">> Requesting MIDI access");
        const midiAccess = await navigator.requestMIDIAccess(this.MIDI_request_option);
        console.log(">> MIDI access granted");
        this.handleMIDIAccess(midiAccess);
        midiAccess.onstatechange = (event) => this.handleStateChange(event);
        return true;
      } catch (error) {
        console.log(">> ERROR: MIDI access failed");
        console.error(error);
        return false;
      }
    }
  
    // Handle MIDI access
    handleMIDIAccess(midiAccess) {
      console.log(">> Available outputs:");
      for (const entry of midiAccess.outputs) {
        const output = entry[1];
        if (output.name.includes("minichord") && output.name.includes("1") || output.name == "minichord") {
          console.log(
            `>>>> minichord sysex control port [type:'${output.type}'] id: '${output.id}' manufacturer: '${output.manufacturer}' name: '${output.name}' version: '${output.version}'`
          );
          this.device = output;
          const sysex_message = [0xF0, 0, 0, 0, 0, 0xF7];
          this.device.send(sysex_message);
        } else {
          console.log(
            `>>>> Other port [type:'${output.type}'] id: '${output.id}' manufacturer: '${output.manufacturer}' name: '${output.name}' version: '${output.version}'`
          );
        }
      }
      console.log(">> Available inputs:");
      for (const entry of midiAccess.inputs) {
        const input = entry[1];
        if (input.name.includes("minichord") && input.name.includes("1") || input.name == "minichord") {
          input.onmidimessage = (message) => this.processCurrentData(message);
          console.log(
            `>>>> minichord sysex control port [type:'${input.type}'] id: '${input.id}' manufacturer: '${input.manufacturer}' name: '${input.name}' version: '${input.version}'`
          );
        } else {
          console.log(
            `>>>> Other port [type:'${input.type}'] id: '${input.id}' manufacturer: '${input.manufacturer}' name: '${input.name}' version: '${input.version}'`
          );
        }
      }
      if (this.device == false) {
        console.log(">> ERROR: no minichord device found");
        if (this.onConnectionChange) {
          this.onConnectionChange(false, "Make sure the minichord is connected to the computer and turned on");
        }
      } else {
        console.log(">> minichord succesfully connected");
      }
    }
  
    // Handle MIDI state changes
    handleStateChange(event) {
      console.log(">> MIDI state change received");
      console.log(event);
      if (event.port.state == "disconnected" && this.device != false && (event.port.name == "minichord Port 1" || event.port.name == "minichord")) {
        console.log(">> minichord was disconnected");
        this.device = false;
        if (this.onConnectionChange) {
          this.onConnectionChange(false, "> minichord disconnected, please reconnect");
        }
      }
      if (event.port.state == "connected" && this.device == false && (event.port.name == "minichord Port 1" || event.port.name == "minichord")) {
        console.log(">> a new device was connected");
        this.handleMIDIAccess(event.target);
      }
    }
  
    // Process incoming MIDI data
    processCurrentData(midiMessage) {
      const data = midiMessage.data.slice(1);
      if (data.length != this.parameter_size * 2 + 1) {
        console.log(">> Non-sysex message received, ignoring");
      } else {
        const processedData = {
          parameters: [],
          rhythmData: [],
          bankNumber: data[2 * 1],
          firmwareVersion: 0
        };
        
        for (var i = 2; i < this.parameter_size; i++) {
          const sysex_value = data[2 * i] + 128 * data[2 * i + 1];
          if (i == this.firmware_adress) {
            processedData.firmwareVersion = sysex_value;
            if (processedData.firmwareVersion < this.min_firmware_accepted) {
              alert("Please update the minichord firmware");
            }
          } else if (i < this.base_adress_rythm + 16 && i > this.base_adress_rythm - 1) {
            const j = i - this.base_adress_rythm;
            const rhythmBits = [];
            for (var k = 0; k < 7; k++) {
              rhythmBits[k] = !!(sysex_value & (1 << k));
            }
            processedData.rhythmData[j] = rhythmBits;
          } else {
            processedData.parameters[i] = sysex_value;
          }
        }
        
        this.active_bank_number = processedData.bankNumber;

        // A faithful copy of the dump, rhythm included, taken before the pot and
        // volume re-centring below overwrites addresses 2-6. Reading a bank to
        // write it back needs what the bank actually holds.
        processedData.rawParameters = [];
        for (var i = 0; i < this.parameter_size; i++) {
          processedData.rawParameters[i] = data[2 * i] + 128 * data[2 * i + 1];
        }
        
        // Override potentiometer and volume values
        for (const i of this.potentiometer_memory_adress) {
          this.sendParameter(i, 512);
          processedData.parameters[i] = 512;
        }
        
        for (const i of this.volume_memory_adress) {
          this.sendParameter(i, 0.5 * 100);
          processedData.parameters[i] = 0.5 * 100;
        }
        
        if (this.onDataReceived) {
          this.onDataReceived(processedData);
        }
      }
    }
  
    // Send parameter to device
    sendParameter(address, value) {
      if (!this.device) return false;
      const first_byte = parseInt(value % 128);
      const second_byte = parseInt(value / 128);
      const first_byte_address = parseInt(address % 128);
      const second_byte_address = parseInt(address / 128);
      const sysex_message = [0xF0, first_byte_address, second_byte_address, first_byte, second_byte, 0xF7];
      this.device.send(sysex_message);
      return true;
    }
  
    // Ask the device to report its live parameters (control command 0).
    requestCurrentData() {
      if (!this.device) return false;
      this.device.send([0xF0, 0, 0, 0, 0, 0xF7]);
      return true;
    }

    // Ask the device to load a bank (control command 4). The preset buttons are
    // otherwise the only way to change bank, so without this the page cannot
    // walk the banks to read them.
    loadBank(bankNumber) {
      if (!this.device) return false;
      this.device.send([0xF0, 0, 0, 4, bankNumber, 0xF7]);
      return true;
    }

    // Load a bank and resolve with its stored parameters. Chains onto the
    // existing callback for one dump rather than replacing it; `quiet` keeps
    // that callback from running, so a walk over twelve banks does not redraw
    // the page twelve times.
    readBank(bankNumber, timeoutMs, quiet) {
      if (!this.device) return Promise.reject(new Error("not connected"));
      return new Promise((resolve, reject) => {
        const previous = this.onDataReceived;
        let settled = false;
        let nudges = [];
        const timer = setTimeout(() => {
          if (settled) return;
          settled = true;
          nudges.forEach(clearTimeout);
          this.onDataReceived = previous;
          reject(new Error("timed out reading bank " + (bankNumber + 1)));
        }, timeoutMs || 3000);
        this.onDataReceived = data => {
          if (previous && !quiet) previous(data);
          if (settled) return;
          // A dump says which bank it describes, and it has to be checked. Loading
          // a bank reports on its own, so a dump from the previous step of a walk
          // can still be in flight; taking it would read the bank before the one
          // asked for, and writing that back copies one preset over another.
          if (data.bankNumber !== bankNumber) return;
          settled = true;
          clearTimeout(timer);
          nudges.forEach(clearTimeout);
          this.onDataReceived = previous;
          resolve(data.rawParameters);
        };
        // Loading reports back, but ask again in case the report is missed. An
        // early ask can be answered by a dump of the previous bank, which is
        // ignored above, so it repeats.
        this.loadBank(bankNumber);
        nudges = [80, 400, 900].map(ms => setTimeout(() => {
          if (!settled) this.requestCurrentData();
        }, ms));
      });
    }

    // Reset memory
    resetMemory() {
      if (!this.device) return false;
      const sysex_message = [0xF0, 0, 0, 1, 0, 0xF7];
      this.device.send(sysex_message);
      return true;
    }
  
    // Save current settings
    saveCurrentSettings(bankNumber) {
      if (!this.device) return false;
      const sysex_message = [0xF0, 0, 0, 2, bankNumber, 0xF7];
      this.device.send(sysex_message);
      return true;
    }
  
    // Reset current bank
    resetCurrentBank() {
      if (!this.device || this.active_bank_number == -1) return false;
      const sysex_message = [0xF0, 0, 0, 3, this.active_bank_number, 0xF7];
      this.device.send(sysex_message);
      return true;
    }
  
    // Check if device is connected
    isConnected() {
      return this.device !== false;
    }
  
    // Get device info
    getDeviceInfo() {
      return {
        connected: this.isConnected(),
        activeBankNumber: this.active_bank_number,
        parameterSize: this.parameter_size,
        colorHueAddress: this.color_hue_sysex_adress,
        baseAddressRhythm: this.base_adress_rythm,
        floatMultiplier: this.float_multiplier
      };
    }

    
  }
  