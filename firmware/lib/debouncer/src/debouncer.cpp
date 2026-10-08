#include "debouncer.h"

debouncer::debouncer(){
}  

void debouncer::set(bool set_value){
    if(value!=set_value){
            flag=true;
            last_update=0;
            value=set_value;
        }
}

uint8_t debouncer::read_transition(){
    if(flag==true && last_update>debounce_value){
        flag=false;
        if(value==true){
            return 2;
        }else{
            return 1;  
        }
    }
    return 0;
}
void debouncer::set_debounce(uint16_t microseconds){
    debounce_value=microseconds;
}
bool debouncer::read_value(){
    return value;
}