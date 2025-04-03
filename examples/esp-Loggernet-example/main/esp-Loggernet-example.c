#include <stdio.h>
#include "esp-Loggernet.h"

#define Client_ID "MyID"
#define uri_default  "mqtt://MyUser:MyPassword@broker.adress.com:1883" 

void app_main(void)
{
  
    init_espLoggernet(uri_default, Client_ID) ;
}

