#include "debouncer.h"

debouncer::debouncer(){
}  

/* set() used to write value straight through and only gate read_transition() on
 * the debounce period. So read_value() was the raw pin state: nothing was ever
 * held steady, only the reporting of edges was delayed.
 *
 * That shows up wherever a combination of buttons is read by value rather than
 * by transition. The three chord type buttons do not release in the same
 * instant, so letting go of a major seventh reads as a plain major on the way
 * out, and anything recalculating in that window picks up the wrong chord.
 *
 * value now changes only once the reading has held for the debounce period, and
 * the flag is raised at that moment rather than at the raw edge. Transition
 * latency is unchanged - it was already the same period, just measured from the
 * other end - so anything using read_transition() behaves exactly as before.
 */
void debouncer::set(bool set_value){
    if(pending!=set_value){
        pending=set_value;
        last_update=0;
        return;
    }
    // A press may be taken sooner than a release (set_press): measured on the chord buttons, a
    // press hardly ever bounces, a release often does, for up to 30 ms, so the press's wait was
    // the whole of a button's latency. Just after a release, a closing is more likely its chatter
    // than a new press, so there a press waits the full debounce as before.
    uint32_t need = (pending && since_release >= relock_value) ? press_value : debounce_value;
    if(pending!=value && last_update>need){
        value=pending;
        flag=true;
        if(!value) since_release=0;
    }
}

uint8_t debouncer::read_transition(){
    if(flag==true){
        flag=false;
        if(value==true){
            return 2;
        }else{
            return 1;  
        }
    }
    return 0;
}
// The latest reading, before it has held for the debounce period. Almost
// everything wants read_value(); this is for a caller that has to know a
// contact closed now, not ten milliseconds from now -- the key change combo,
// which must claim a button pressed in its last moments before the press
// becomes a value the chords can see.
bool debouncer::read_raw(){
    return pending;
}
void debouncer::set_press(uint16_t microseconds, uint32_t relock){
    press_value=microseconds;
    relock_value=relock;
}
void debouncer::set_debounce(uint16_t microseconds){
    debounce_value=microseconds;
    press_value=microseconds;
}
bool debouncer::read_value(){
    return value;
}
