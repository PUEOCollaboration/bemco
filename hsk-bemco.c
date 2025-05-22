#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <errno.h>
#include <time.h>
#include <sys/file.h>
#include <stdint.h>
#include "libpq-fe.h"
#include <stdio.h>

const char * serial_device= "/dev/ttyHK";


struct Timestruct_float{
    float value;
    uint32_t timestamp;
}__attribute__((packed));

struct Timestruct_uint16_t{
    int16_t value;
    uint32_t timestamp;
}__attribute__((packed));

struct CCReadings{
    struct Timestruct_uint16_t Scalings[4];
    struct Timestruct_uint16_t ADC[11];
    struct Timestruct_float Temp[2];
    struct Timestruct_uint16_t ADCSlow[7];
    struct Timestruct_uint16_t LEDState[2];
    struct Timestruct_uint16_t Charger[6];
    struct Timestruct_uint16_t MPPT[5];
}__attribute__((packed));


static int do_read(int fd, void * where, int len)
{
  int rd = 0;
  while (rd < len)
  {

    int r = read(fd, where+rd, len-rd);
    if (r < 0)
    {
      printf("Read returned %d: errno %d, %s\n", r, errno, strerror(errno));
      return -1;
    }
    else
    {
      rd += r;
    }
  }

  return rd;
}

static void adc_conv(uint16_t val, float * save_V, float * save_I, float * save_T)
{
  float V = (val - 32768) * (1.25 / 32768);
  if (save_V) *save_V = 20*V;


  if (save_T)
  {
    const float offset= -1481.96;
    const float v_offset=1.8639;
    const float sqrt_offset=2196200.0;
    const float sqrt_scale=0.00000388;
    *save_T =  (offset+sqrt(sqrt_offset+((v_offset-(20*V))/sqrt_scale)));
  }

  if (save_I)
  {
    *save_I = V / 0.0625;
  }

}

typedef struct measurement_type
{
  const char* label;
  enum { UNKNOWN, VOLTAGE, CURRENT, TEMPERATURE} type;
  double scale;
} measurement_type_t;



static measurement_type_t
extana_map [2][30] =
{
  [0][0] = { .label =  "5V Vicor", .type = VOLTAGE},
  [0][1] = { .label =  "5V Vicor", .type = CURRENT},
  [0][2] = { .label =  "4V Vicor", .type = VOLTAGE},
  [0][3] = { .label =  "4V Vicor", .type = CURRENT},
  [0][4] = { .label =  "RB1", .type = TEMPERATURE},
  [0][5] = { .label =  "RB2", .type = TEMPERATURE},
  [0][6] = { .label =  "RB3", .type = TEMPERATURE},
  [0][7] = { .label =  "RB4", .type = TEMPERATURE},
  [0][8] = { .label =  "RB5", .type = TEMPERATURE},
  [0][9] = { .label =  "RB6", .type = TEMPERATURE},
  [0][11] = { .label =  "12V_A V", .type = VOLTAGE},
  [0][12] = { .label =  "12V_A I", .type = CURRENT},
  [0][13] = { .label =  "12V_B V", .type = VOLTAGE},
  [0][14] = { .label =  "12V_B I", .type = CURRENT},
  [0][15] = { .label =  "12V_C V", .type = VOLTAGE},
  [0][16] = { .label =  "12V_C I", .type = CURRENT},
  [0][24] = { .label =  "3.3V rail", .type = VOLTAGE},
  [0][25] = { .label =  "12V rail", .type = VOLTAGE},

};

static measurement_type_t powerana_map[11] =
{
  [0] = { .label = "RF_OFF_INV", .type = VOLTAGE },
  [2] = { .label = "BATTERY", .type = VOLTAGE, .scale = 3.39},
  [3] = { .label = "24V", .type = VOLTAGE },
  [4] = { .label = "SOLAR", .type = VOLTAGE, .scale = 3.39},
  [5]  = { .label = "12VD", .type = VOLTAGE },
};

int main(int nargs, char ** args)
{

  //opend evice, lock, flush
  int fd = open(serial_device, O_RDWR);
  flock(fd, LOCK_EX);
  tcflush(fd, TCIOFLUSH);


  uint8_t seqnum = time(NULL) & 0xff;
  uint8_t rcv_seqnum = 0;

  // get the iiiiuptime
  struct header
  {
    uint8_t magic;
    uint8_t cmd;
    uint16_t len;
  } hd = {.magic = 0xfc, .cmd = 0x54, .len = 0 };


  struct timespec now;

  uint64_t uptime;
  write(fd,&hd, sizeof(hd));
  write(fd,&seqnum, sizeof(seqnum));
  tcdrain(fd);
  int rd_uptime = do_read(fd, &hd, sizeof(hd));
  rd_uptime  += do_read(fd, &uptime, sizeof(uptime));
  rd_uptime  += do_read(fd, &rcv_seqnum, sizeof(rcv_seqnum));
  printf("%d hd[M:0x%02hhx,C:0x%02hhx,L:0x%02hx]\n", rd_uptime, hd.magic, hd.cmd, hd.len); 
  clock_gettime(CLOCK_REALTIME, &now);

  printf("Now  is %d.%09d\n", now.tv_sec, now.tv_nsec);

  if (rcv_seqnum != seqnum) fprintf(stderr,"seqnum mismatch for uptime\n");
  else
  {
    printf("uptime = %u (%f hours)\n", uptime, uptime / 1e3 / 3600);

  }


  double double_uptime = uptime;
  double_uptime /= 1e3;

  double now_double = now.tv_sec;
  now_double += now.tv_nsec/1e9;
  double offset  = now_double - double_uptime;
  printf ("%f %f\n", now_double, offset);
  struct
  {
    float temp;
    uint32_t ts;
  } temps[74] = {};


  hd.cmd = 0x30;
  hd.len = 0;
  tcflush(fd, TCIOFLUSH);
  write(fd, &hd, sizeof(hd));
  write(fd, &seqnum, sizeof(seqnum));
  tcdrain(fd);

  seqnum++;

  int nrd = 0;
  nrd += do_read(fd, &hd, sizeof(hd));
  printf("hd[M:0x%02hhx,C:0x%02hhx,L:0x%02hx]\n", hd.magic, hd.cmd, hd.len); 
  nrd += do_read(fd, temps, sizeof(temps));
  nrd += do_read(fd, &rcv_seqnum, sizeof(rcv_seqnum));

  printf("temps: [%d bytes] S:%hhu/%hhu]", nrd, seqnum, rcv_seqnum);

  struct
  {
    uint16_t val;
    uint32_t ts;
  } __attribute__((packed)) extana [2][30] = {};


  seqnum++;

  hd.cmd = 0x20;
  hd.len = 0;
  tcflush(fd, TCIOFLUSH);
  write(fd, &hd, sizeof(hd));
  write(fd, &seqnum, sizeof(seqnum));
  tcdrain(fd);

  nrd = 0;

  nrd += do_read(fd, &hd, sizeof(hd));
  nrd += do_read(fd, extana, sizeof(extana));
  rcv_seqnum = 0;
  nrd += do_read(fd, &rcv_seqnum, sizeof(rcv_seqnum));

  printf("hd[M:0x%02hhx,C:0x%02hhx,L:0x%02hx]\n", hd.magic, hd.cmd, hd.len);
  printf("extana: [%d bytes] S:%hhu/%hhu\n", nrd, seqnum, rcv_seqnum);


  struct
  {
    float val;
    uint32_t ts;
  } pwr[11] = {};

  hd.cmd = 0x14;
  hd.len = 0;
  tcflush(fd, TCIOFLUSH);
  write(fd, &hd, sizeof(hd));
  write(fd, &seqnum, sizeof(seqnum));
  tcdrain(fd);
  nrd += do_read(fd, &hd, sizeof(hd));
  nrd += do_read(fd, pwr, sizeof(pwr));
  rcv_seqnum = 0;
  nrd += do_read(fd, &rcv_seqnum, sizeof(rcv_seqnum));

  printf("hd[M:0x%02hhx,C:0x%02hhx,L:0x%02hx]\n", hd.magic, hd.cmd, hd.len);
  printf("pwrana: [%d bytes] S:%hhu/%hhu\n", nrd, seqnum, rcv_seqnum);

  hd.cmd = 0x60;
  hd.len = 0;

  tcflush(fd, TCIOFLUSH);
  write(fd, &hd, sizeof(hd));
  write(fd, &seqnum, sizeof(seqnum));
  tcdrain(fd);

  struct CCReadings cc = {};

  nrd += do_read(fd, &hd, sizeof(hd));
  nrd += do_read(fd, &cc, sizeof(cc));
  rcv_seqnum = 0;
  nrd += do_read(fd, &rcv_seqnum, sizeof(rcv_seqnum));

  printf("hd[M:0x%02hhx,C:0x%02hhx,L:0x%02hx]\n", hd.magic, hd.cmd, hd.len);
  printf("cc: [%d bytes] S:%hhu/%hhu\n", nrd, seqnum, rcv_seqnum);


  close(fd);



  char * conninfo = getenv("BEMCO_CONNINFO");

  static PGconn * pgsql = 0;
  if (conninfo)
  {
    pgsql = PQconnectdb(conninfo);
    bool dbok = (PQstatus(pgsql) == CONNECTION_OK);
    if (!dbok)
    {
      fprintf(stderr,"Could not connect to pgsql (%s)\n", conninfo);
      conninfo = 0;
    }
    else
    {
      printf("psql connection ok\n");
    }
  }

  static char buf[512];
  printf("Temperatures:\n");
	for (int i = 0; i < 27; i++)
	{
    double meas_time = temps[i].ts/1000 + offset;

		printf(": T%d: %f @ %u (delta=-%f, or %f)\n", i, temps[i].temp, temps[i].ts, now_double-meas_time, meas_time );

    if (conninfo)
    {
      snprintf(buf,sizeof(buf),"insert into temperatures (time, device,sensor,temperature) values (to_timestamp(%f), 'HK','T%d',%f);", meas_time,i,temps[i].temp);
      PGresult * r = PQexec(pgsql,buf);
      if (PQresultStatus(r) != PGRES_COMMAND_OK) printf("%s\n", PQresultErrorMessage(r));
      PQclear(r);
    }
	}

  printf("Extana:\n");
	for (int i = 0; i < 30; i++)
	{
     if (!extana_map[0][i].type) continue; 
     double meas_time = extana[0][i].ts/1000 + offset;

     float as_V, as_I, as_T;
     adc_conv(extana[0][i].val, &as_V, &as_I, &as_T);
     float val = extana_map[0][i].type == TEMPERATURE ? as_T : extana_map[0][i].type == CURRENT ? as_I : as_V;
     printf(": %s: %f %s (adc = %d) @ %f\n",
     extana_map[0][i].label, val,
     extana_map[0][i].type == TEMPERATURE ? "C" :extana_map[0][i].type == CURRENT ? "A" : "V",
     extana[0][i].val - 32678, meas_time);

     if (conninfo)
     {
       const char * what = extana_map[0][i].type == TEMPERATURE ? "temperature" : extana_map[0][i].type == VOLTAGE ? "voltage" : "current"; 
       snprintf(buf,sizeof(buf),"insert into %ss (time, device,sensor,%s) values (to_timestamp(%f), 'HK','%s',%f);", what, what, meas_time,extana_map[0][i].label,val);
       PGresult * r = PQexec(pgsql,buf);
       if (PQresultStatus(r) != PGRES_COMMAND_OK) printf("%s\n", PQresultErrorMessage(r));
       PQclear(r);
     }
	}

  printf("Powerana:\n");

	for (int i = 0; i < 11; i++)
	{
     if (!powerana_map[i].type) continue; 
     double meas_time = pwr[i].ts/1000 + offset;
     float val = powerana_map[i].scale == 0 ? pwr[i].val : pwr[i].val * powerana_map[i].scale;
     printf(":%s: %f %s @ %f\n",
     powerana_map[i].label, val,
     powerana_map[i].type == TEMPERATURE ? "C" : powerana_map[i].type == CURRENT ? "A" : "V", meas_time);

     if (conninfo)
     {
       const char * what = powerana_map[i].type == TEMPERATURE ? "temperature" : powerana_map[i].type == VOLTAGE ? "voltage" : "current";
       snprintf(buf,sizeof(buf),"insert into %ss (time, device,sensor,%s) values (to_timestamp(%f), 'HK','%s',%f);", what, what, meas_time,powerana_map[i].label,val);
       PGresult * r = PQexec(pgsql,buf);
       if (PQresultStatus(r) != PGRES_COMMAND_OK) printf("%s\n", PQresultErrorMessage(r));
       PQclear(r);
     }
  }


  printf("Charge Controller\n");
  float VPU_full=(float) (cc.Scalings[0].value) + (float)(cc.Scalings[1].value)/65536.0;
  float IPU_full=(float) (cc.Scalings[2].value) + (float)(cc.Scalings[3].value)/65536.0;
  printf("Scalings\n");
  for (int i = 0; i < 4; i++) 
  {
    printf(" %d: %hu @ %u\n", i, cc.Scalings[i].value, cc.Scalings[i].timestamp);
  }
  printf("ADC\n");

  for (int i = 0; i < 11; i++)
  {
    float pretty = 
           i < 4 ? cc.ADC[i].value * VPU_full / 32768. :
           i > 6 && i < 8 ? cc.ADC[i].value * IPU_full /32768. : 
           i == 6 || i == 8 ? cc.ADC[i].value *18.612 / 32768 : 
           i == 8 ? cc.ADC[i].value * 6.6 / 32768 : 
           cc.ADC[i].value  * 3. / 32768 ;
      printf(" %d: %hu = %f @ %u\n", i, cc.ADC[i].value, pretty, cc.ADC[i].timestamp);
  }

  if (cc.Temp[0].value > 32767) cc.Temp[0].value-=65536;
  if (cc.Temp[1].value > 32767) cc.Temp[1].value-=65536;
  printf("Temp: %f,%f @ %u,%u\n", cc.Temp[0].value, cc.Temp[1].value, cc.Temp[0].timestamp, cc.Temp[1].timestamp);
  if (conninfo)
    {
      for (int i = 0; i < 2; i++)
      {
         double meas_time = cc.Temp[i].timestamp/1000 + offset;
         snprintf(buf,sizeof(buf),
             "insert into temperatures (time, device,sensor,temperature) values (to_timestamp(%f), 'HK','%s',%f);", 
              meas_time,i == 0 ? "CCTempInternal" : "CCTempRemote",cc.Temp[i].value);
         PGresult * r = PQexec(pgsql,buf);
         if (PQresultStatus(r) != PGRES_COMMAND_OK) printf("%s\n", PQresultErrorMessage(r));
         PQclear(r);
      }
    }
 
  printf("ADCSlow\n");

  for (int i = 0; i < 7; i++)
  {
    double meas_time = cc.ADCSlow[i].timestamp/1000 + offset;
    float pretty =
           i == 0 || i == 2 || i == 3 ? cc.ADCSlow[i].value * VPU_full / 32768. :
           i == 1 ? cc.ADCSlow[i].value * IPU_full / 32768. :
           (float) cc.ADCSlow[i].value;
    const char * labels[] = { "CCFilteredBattVoltage","CC_ChargingCurrent","CC_BATT_MIN","CC_BATT_MAX","CC_HOURS_HIGH","CC_HOURS_LOW","CC_FAULT"};
    if (conninfo)
    {
       char * what = i == 1 ? "current" : "voltage";
       snprintf(buf,sizeof(buf),
           "insert into %ss (time, device,sensor,%s) values (to_timestamp(%f), 'HK','%s',%f);", 
           what, what, meas_time,labels[i],pretty);
       PGresult * r = PQexec(pgsql,buf);
       if (PQresultStatus(r) != PGRES_COMMAND_OK) printf("%s\n", PQresultErrorMessage(r));
       PQclear(r);
    }
    printf(" %d: %hu  = %f @ %u\n", i, cc.ADCSlow[i].value, pretty, cc.ADCSlow[i].timestamp);
  }

  printf("LEDState: 0x%hx  0x%hx @ %u, %u\n",
      cc.LEDState[0].value, cc.LEDState[1].value,
      cc.LEDState[0].timestamp, cc.LEDState[1].timestamp);
 printf("Charger\n");
 for (int i = 0; i < 6; i++) printf(" %d: %hu @ %u\n", i, cc.Charger[i].value, cc.Charger[i].timestamp);
 printf("MPPT\n");
 for (int i = 0; i < 5; i++)
 {
   double meas_time = cc.MPPT[i].timestamp/1000 + offset;
   float pretty = cc.MPPT[i].value * VPU_full / 32768.;
   if (i < 3)   pretty *= IPU_full / 4;

    const char * labels[] = { "CC_Output_Power","CC_Input_Power","CC_MaxLastSweep_Power","CC_VmpLastSweep","CC_VocLastSweep"};
    if (conninfo)
    {
       char * what = i < 3 ? "power" : "voltage";
       snprintf(buf,sizeof(buf),
           "insert into %ss (time, device,sensor,%s) values (to_timestamp(%f), 'HK','%s',%f);", 
           what, what, meas_time,labels[i],pretty);
       PGresult * r = PQexec(pgsql,buf);
       if (PQresultStatus(r) != PGRES_COMMAND_OK) printf("%s\n", PQresultErrorMessage(r));
       PQclear(r);
    }
   printf(" %d (%f) : %hu @ %u\n", i, cc.MPPT[i].value, pretty, cc.MPPT[i].timestamp);
 }




  if (conninfo)
    PQfinish(pgsql);

  return 0;
}
