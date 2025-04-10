#include <sensors/sensors.h>
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

static int nmeas = 0;
static struct cpu_temp_sensor * head = NULL;

typedef void (*measure_callback_t)(int inum, const char * chip_label, const char * label, double val);


#define MAX_SENSORS 256
static struct sensor_val
{
  const sensors_chip_name *cn;
  int subnr;
  char * sensor_label;
  const char * chip_label;
  double value;
} sensors[MAX_SENSORS];

static int num_sensors = 0;


void probe()
{

  const sensors_chip_name * cn = 0;
  int ichip =0;
  while ((cn = sensors_get_detected_chips(0,&ichip)) != 0)
  {
    char * chipname = malloc(128);
    sensors_snprintf_chip_name(chipname,128,cn);
    const sensors_feature * feat = 0;
    int ifeature = 0;
    while ((feat = sensors_get_features(cn, &ifeature)) !=0)
    {
      const sensors_subfeature * subf = 0;
      if (feat->type != SENSORS_FEATURE_TEMP) continue; //not a temperature sensor
      int isubf = 0;
      while ((subf = sensors_get_all_subfeatures(cn, feat, &isubf)) !=0)
      {

        double val = -1;

        if ((subf->type == SENSORS_SUBFEATURE_TEMP_INPUT)
          && (subf->flags & SENSORS_MODE_R))
        {
          sensors[num_sensors].cn = cn;
          sensors[num_sensors].subnr = subf->number;
          sensors[num_sensors].sensor_label = sensors_get_label(cn, feat);
          sensors[num_sensors].chip_label = chipname;
          num_sensors++;
        }
      }
    }
  }
}

void measure(measure_callback_t * callbacks)
{
  for (int i = 0; i < num_sensors; i++)
  {
    sensors_get_value(sensors[i].cn, sensors[i].subnr,  &sensors[i].value);

    int icallback = 0;
    while (callbacks[icallback]) callbacks[icallback++](i, sensors[i].chip_label, sensors[i].sensor_label, sensors[i].value);
  }
}

void print_callback(int inum, const char * clabel, const char * label, double val)
{

  printf("Sensor %d (%s/%s): %f\n", inum, clabel, label, val);
}


static PGconn * pgsql = 0;
void psql_callback(int inum, const char * clabel, const char * label, double val)
{
  static char buf[512];
  snprintf(buf,sizeof(buf),"insert into temperatures (time, device,sensor,temperature) values (NOW(), 'CPU','%s:%s',%f);", clabel, label, val);
  PGresult * r = PQexec(pgsql,buf);
  if (PQresultStatus(r) != PGRES_COMMAND_OK) printf("%s\n", PQresultErrorMessage(r));
  PQclear(r);
}



static char tstring[64];
int main(int nargs, char ** args)
{

  signal(SIGINT, guillotine);
  char * conninfo=getenv("BEMCO_CONNINFO");
  if (!conninfo)
  {
    fprintf(stderr,"DEFINE BEMCO_CONNINFO CORRECTLY YOU FOOL\n");
    return 1;
  }

  nvmlInit();
  pgsql = PQconnectdb(conninfo);
  bool dbok = (PQstatus(pgsql) == CONNECTION_OK);
  if (!dbok)
  {
    fprintf(stderr,"Could not connect to pgsql (%s)\n", conninfo);
  }

  sensors_init(NULL);
  signal(SIGINT, guillotine);
  probe();

  nvmlDevice_t dev;
  nvmlDeviceGetHandleByIndex(0, &dev);

  measure_callback_t callbacks[] = { print_callback, dbok ? psql_callback : NULL,  NULL };
  while(!die)
  {
    static char gpu_buf[512];

    time_t t = time(NULL);
    strftime(tstring, sizeof(tstring), "%D %T", gmtime(&t));
    printf("Now is %s\n", tstring);

    unsigned int temperature = 666;
    nvmlDeviceGetTemperature(dev, NVML_TEMPERATURE_GPU, &temperature);
    printf("GPU is %u\n", temperature);
    if (dbok)
    {
      snprintf(gpu_buf,sizeof(gpu_buf),"insert into temperatures (time, device,sensor,temperature) values (NOW(), 'CPU','GPU',%u);",  temperature);
      PGresult * r = PQexec(pgsql,gpu_buf);
      if (PQresultStatus(r) != PGRES_COMMAND_OK) printf("%s\n", PQresultErrorMessage(r));
      PQclear(r);
    }


    measure(callbacks);

    sleep(5);
  }

  PQfinish(pgsql);

  return 0;

}
