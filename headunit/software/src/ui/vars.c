#include "vars.h"

uint8_t bat_lvl = 0;
uint8_t speed = 0;

int32_t get_var_bat_lvl(){
    return bat_lvl;
}

void set_var_bat_lvl(int32_t value){
    bat_lvl = value;
}

int32_t get_var_speed(){
    return speed;
}

void set_var_speed(int32_t value){
    speed = value;
}