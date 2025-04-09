#include <time.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "libpq-fe.h"
#include "nvml.h"

double interval = 1;



volatile int die = 0;

void guillotine(int foo) { (void) foo;  die = 1; }






int main(int nargs, char ** args)
{

  char * conninfo=getenv("BEMCO_CONNINFO");
  if (!conninfo)
  {
    fprintf(stderr,"DEFINE BEMCO_CONNINFO CORRECTLY YOU FOOL\n");
    return 1;
  }

  PGconn * pgsql = PQconnectdb(conninfo);
  bool dbok = (PQstatus(pgsql) == CONNECTION_OK);
  if (!dbok)
  {
    fprintf(stderr,"Could not connect to pgsql (%s)\n", conninfo);
  }

  nvmlInit();
  signal(SIGINT, guillotine);
  nvmlDevice_t dev;
  nvmlDeviceGetHandleByIndex(0, &dev);

  static char buf[512];

  while(!die)
  {

    time_t t = time(NULL);
    static char tstring[64];
    strftime(tstring, sizeof(tstring), "%D %T", gmtime(&t));

    unsigned int temperature = 666;
    nvmlDeviceGetTemperature(dev, NVML_TEMPERATURE_GPU, &temperature);

    printf("Now is %s, GPU is %u\n", tstring, temperature);
    snprintf(buf,sizeof(buf),"insert into temperatures (time, device,sensor,temperature) values (NOW(), 'CPU','GPU',%u);",  temperature);
    PGresult * r = PQexec(pgsql,buf);
    if (PQresultStatus(r) != PGRES_COMMAND_OK) printf("%s\n", PQresultErrorMessage(r));
    PQclear(r);


    sleep(5);
  }

  PQfinish(pgsql);

  return 0;

}
