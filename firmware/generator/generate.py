import json
from airium import Airium
from itertools import groupby
import shutil

a = Airium(
    base_indent='  ',  # str
    current_level=0,  # int
    source_minify=False,  # bool
    source_line_break_character="\n",  # str
    )

cpp_start_file="void apply_audio_parameter(int adress, int value) {\r\n    switch(adress){\r\n"
cpp_end_file="  }\r\n}"
id_iterator=0

#Making the HTML file
with a.html():
    with a.head():
        a.link(href='index.css', rel='stylesheet')

with open('parameters.json') as f: # Reserved adresses: 0 for system command and 1 for bank adress
    d = json.load(f)
    with a.html():
        with a.head():
            a.meta(charset='UTF-8')
            a.meta(content='width=device-width, initial-scale=1.0', name='viewport')
            a.meta(content='ie=edge', **{'http-equiv': 'X-UA-Compatible'})
            a.link(href='../img/android-chrome-192x192.png', rel='icon', sizes='192x192', type='image/png')
            a.link(href='../img/android-chrome-512x512.png', rel='icon', sizes='512x512', type='image/png')
            a.link(href='../img/apple-touch-icon.png', rel='apple-touch-icon', sizes='180x180')
            a.link(href='../img/favicon.ico', rel='shortcut icon', sizes='48x48', type='image/png')
            a.link(href='../img/favicon-16x16.png', rel='icon', sizes='16x16', type='image/png')
            a.link(href='../img/favicon-32x32.png', rel='icon', sizes='32x32', type='image/png')
            a.title(_t='minicontrol')
        with a.body(id="body", klass="control_full"):
            with a.div(id='content',klass="line"):
                with a.div(id="control", klass="bloc B3 M4 S9"):
                    with a.div(id='header',klass="line"):
                        with a.div(klass="title bloc B9 M9 S9"):
                            a.h4(_t='minicontrol')
                        with a.div(id='status_zone', klass="unconnected"):
                            a.span(_t='●',id='dot')
                            a.span(_t='',id='status_value')
                        with a.div():
                            a.button(id='theme-toggle', onclick='toggleTheme()', title='Toggle Dark/Light Mode', **{'aria-label': 'Toggle dark mode'})
                        with a.div():
                            a('To test and load user-submitted presets, visit the ')
                            a.a(href='./minishop.html', _t='minishop.')
                        with a.div(klass="line"):
                            a.h5(_t='saving:',klass="inactive")
                        with a.div(klass="line"):
                            with a.div(klass="bloc B3 M3 S3 button_div"):
                                with a.div( klass="select_container"):
                                    a.span(_t='target bank:', klass="inactive")
                                    with a.select(id='bank_number_selection', klass="inactive", name='bank number'):
                                        for i in range(12):
                                            a.option(value=i, _t=(i+1), klass="inactive")
                            with a.div(klass="bloc B3 M3 S3 button_div"):
                                a.button(onclick='save_current_settings()', _t='save to bank',klass="inactive")
                        with a.div(klass="line"):
                            a.h5(_t='sharing:',klass="inactive")
                        with a.div(klass="line"):
                            with a.div(klass="bloc B3 M3 S3 button_div"):
                                a.button(onclick='generate_settings()', _t='export settings',klass="inactive")
                            with a.div(klass="bloc B3 M3 S3 button_div"):
                                a.button(onclick='load_settings()', _t='load settings',klass="inactive")
                        with a.div(klass="line"):
                            a.h5(_t='resetting:',klass="inactive")
                        with a.div(klass="line"):
                            with a.div(klass="bloc B3 M3 S3 button_div"):
                                a.button(onclick='reset_current_bank()', _t='reset bank',klass="inactive")
                            with a.div(klass="bloc B3 M3 S3 button_div"):
                                a.button(onclick='reset_memory()', _t='reset all banks',klass="inactive")
                    with a.details():
                        with a.summary():
                            a.b(_t='Connection instruction')
                        with a.ul(klass="instruction_steps"):
                            with a.li(id="step1", klass="unsatisfied"):
                                a('provide the system autorisation for MIDI control')
                            with a.li(id="step2", klass="unsatisfied"):
                                a('use a recent version of Chrome')
                            with a.li(id="step3", klass="unsatisfied"):
                                a('connect the minichord with a USB cable and make sure it is on.')
                        with a.div(tabindex='1', id='information_zone'):
                            a.strong(_t='> Please follow the steps above',id='information_text')
                    
                    with a.div( id='instruction_zone'):
                        a('For instruction on how to use this tool, please refer to the ')
                        a.a(href='../user_manual/#custom-presets', _t='minichord documentation.')
                a.div(id="spacer", klass="bloc B0 M0 S0")
                with a.div(id="parameters", klass="bloc B8 M8 S9"):
                    with a.div(klass="array_content"):
                        with a.div(klass="filter_bar"):
                            a.input(id="parameter_filter", type="search", placeholder="filter parameters by name, group or id",
                                    oninput="filter_parameters(this)", autocomplete="off")
                            a.span(id="filter_count", klass="filter_count")
                    # One pass per section. The three used to be copies of each
                    # other; a parameter added to one had to be added three times.
                    sections = [("global_parameter", "Global parameters"),
                                ("harp_parameter", "Harp parameters"),
                                ("chord_parameter", "Chord parameters")]
                    for section_index, (section_key, section_title) in enumerate(sections):
                        if section_key not in d:
                            print("Missing " + section_key + " in JSON")
                            continue
                        with a.div(klass="array_content"):
                            with a.div(name=id_iterator, klass="line header_data data_line inactive section_line"):
                                with a.div(klass=" bloc B8 M8 S8"):
                                    a.p(_t=section_title, klass="row_title")
                                with a.div(klass=" bloc B1 M1 S1 right_aligned"):
                                    if section_index == 0:
                                        a.button(id="randomise_btn", title="Randomise all values", klass="icon_button", _t="\U0001F3B2")
                            # Column labels, once per section, so a long list stays readable
                            with a.div(klass="line data_line column_labels"):
                                with a.div(klass=" bloc B1 M1 S1"):
                                    a.p(_t="id")
                                with a.div(klass=" bloc B7 M4 S5"):
                                    a.p(_t="name")
                                with a.div(klass=" bloc B7 M4 S7"):
                                    a.p(_t="control")
                                with a.div(klass=" bloc B2 M1 S2 right_aligned"):
                                    a.p(_t="value")
                            with a.div(name=id_iterator, klass="line data_line"):
                                a.hr()
                            for group_name, parameter_group in groupby(d[section_key], lambda content: content["group"]):
                                parameters = list(parameter_group)
                                # Some parameters share a name within a group -- the
                                # sixteen rythm pattern steps, for instance -- which
                                # leaves sixteen identical labels. Number the repeats
                                # so each row says which one it is. Purely a label:
                                # the address is what identifies it.
                                repeated = {}
                                for parameter in parameters:
                                    repeated[parameter["name"]] = repeated.get(parameter["name"], 0) + 1
                                seen = {}
                                group_class = "line data_line group_line"
                                if group_name == "hidden":
                                    group_class += " hidden"
                                # The group name used to sit in a cell on its first row
                                # only, so scrolling lost it. It is its own row now, and
                                # it sticks to the top of the viewport while its
                                # parameters are on screen.
                                with a.div(klass=group_class):
                                    with a.div(klass=" bloc B9 M9 S9"):
                                        a.p(_t=group_name, klass="group_title")
                                for parameter in parameters:
                                    try:
                                        row_class = "line data_line content_line inactive"
                                        if group_name == "hidden":
                                            row_class += " hidden"
                                        label = parameter["name"]
                                        if repeated[label] > 1:
                                            seen[label] = seen.get(label, 0) + 1
                                            label = label + " " + str(seen[label])
                                        search_key = (str(label) + " " + str(group_name) + " "
                                                      + str(section_title) + " " + str(parameter["sysex_adress"])).lower()
                                        with a.div(name=id_iterator, version=parameter["introduction_version"],
                                                   klass=row_class, data_search=search_key):
                                            with a.div(klass=" bloc B1 M1 S1"):
                                                a.p(_t=parameter["sysex_adress"])
                                            with a.div(klass=" bloc B7 M4 S5"):
                                                a.dfn(title=parameter["tooltip"], _t=label)
                                            with a.div(klass=" bloc B7 M4 S7"):
                                                if(parameter["data_type"]=="degrees"):
                                                    # a row of twelve checkboxes, one per chromatic degree,
                                                    # all writing bits of a single parameter
                                                    with a.span(klass="degree_row", adress_field=parameter["sysex_adress"], id=id_iterator):
                                                        for bit, degree_label in enumerate(["1","b2","2","b3","3","4","b5","5","b6","6","b7","7"]):
                                                            with a.label(klass="degree"):
                                                                a.input(klass="degree_box inactive", adress_field=parameter["sysex_adress"],
                                                                        data_type="degrees", bit=bit, onchange='handledegree(this)', type='checkbox')
                                                                a.span(_t=degree_label)
                                                elif(parameter["data_type"]=="int"):
                                                    a.input(klass="slider inactive",adress_field=parameter["sysex_adress"], curve=parameter["curve"],data_type=parameter["data_type"], id=id_iterator, max=parameter["max_value"], min=parameter["min_value"], onchange='handlechange(this)', step='1', target_max=parameter["max_value"], target_min=parameter["min_value"], type='range', value=parameter["default_value"])
                                                else:
                                                    a.input(klass="slider inactive",adress_field=parameter["sysex_adress"], curve=parameter["curve"],data_type=parameter["data_type"], id=id_iterator, max=parameter["max_value"], min=parameter["min_value"], onchange='handlechange(this)', step='0.01', target_max=parameter["max_value"], target_min=parameter["min_value"], type='range', value=parameter["default_value"])
                                            with a.div(klass=" bloc B2 M1 S2"):
                                                a.p(id="value_zone"+str(parameter["sysex_adress"]),klass='value_zone')
                                            id_iterator+=1
                                    except KeyError:
                                        print("Missing entry parameter in the JSON item : ")
                                        print(parameter)
                                        break
            a.script(src='javascript/minichordcontroller.js')
            a.script(src='javascript/index.js')
            a.p(id="output_zone")

Html_file= open("../minicontrol/index.html","w")
Html_file.write(str(a))
Html_file.close()

#Making the CPP file


with open('parameters.json') as f:
    d = json.load(f)
    with open('../include/sysex_handler.h', 'w') as cpp_output:
        cpp_output.write(cpp_start_file)
        if "global_parameter" in d:
            try:
                for parameter in d["global_parameter"]:
                    method=parameter["method"]
                    if(parameter["data_type"]=="float"):
                        method=method.replace("value","value/100.0")
                    cpp_output.write("      case "+str(parameter["sysex_adress"])+":\r\n")
                    if(parameter["iterate"]>1):
                        cpp_output.write("        for (int i=0;i<"+str(parameter["iterate"])+";i++){\r\n")
                        cpp_output.write("          "+method+"\r\n")
                        cpp_output.write("        }\r\n")
                    else:
                        cpp_output.write("        "+method+"\r\n")
                    cpp_output.write("        break;\r\n")

            except KeyError:
                print("Missing entry parameter in the JSON item : ")
                print(parameter)
        else:
            print("Missing global parameters in JSON")
        if "harp_parameter" in d:
            try:
                for parameter in d["harp_parameter"]:
                    method=parameter["method"]
                    if(parameter["data_type"]=="float"):
                        method=method.replace("value","value/100.0")
                    cpp_output.write("      case "+str(parameter["sysex_adress"])+":\r\n")
                    if(parameter["iterate"]>1):
                        cpp_output.write("        for (int i=0;i<"+str(parameter["iterate"])+";i++){\r\n")
                        cpp_output.write("          "+method+"\r\n")
                        cpp_output.write("        }\r\n")
                    else:
                        cpp_output.write("        "+method+"\r\n")
                    cpp_output.write("        break;\r\n")

            except KeyError:
                print("Missing entry parameter in the JSON item : ")
                print(parameter)
        else:
            print("Missing harp parameters in JSON")
        if "chord_parameter" in d:
            try:
                for parameter in d["chord_parameter"]:
                    method=parameter["method"]
                    if(parameter["data_type"]=="float"):
                        method=method.replace("value","value/100.0")
                    cpp_output.write("      case "+str(parameter["sysex_adress"])+":\r\n")
                    if(parameter["iterate"]>1):
                        cpp_output.write("        for (int i=0;i<"+str(parameter["iterate"])+";i++){\r\n")
                        cpp_output.write("          "+method+"\r\n")
                        cpp_output.write("        }\r\n")
                    else:
                        cpp_output.write("        "+method+"\r\n")
                    cpp_output.write("        break;\r\n")

            except KeyError:
                print("Missing entry parameter in the JSON item : ")
                print(parameter)
        else:
            print("Missing chord parameters in JSON")
        cpp_output.write(cpp_end_file)

    # Copy the parameters.json file to the ../minicontrol/json folder
    destination_folder = "../minicontrol/json"
    shutil.copy('parameters.json', destination_folder)
