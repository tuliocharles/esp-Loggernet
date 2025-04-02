#include <stdio.h>
#include "esp-Loggernet.h"

#define Client_ID "com2efm"
#define uri_default  "mqtt://efm2com:efm2com@node02.myqtthub.com:1883" 

void app_main(void)
{
  
    init_espLoggernet(uri_default, Client_ID) ;
}

