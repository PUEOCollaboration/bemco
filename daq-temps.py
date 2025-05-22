#! /usr/bin/env python3

import time
import psycopg2
import sys
import os
import signal

from HskSerial.HskSerial import HskEthernet, HskPacket

hsk = HskEthernet()

five_oclock = False
def on_timeout(signum,frame):
    raise TimeoutError()

def on_int(signum, frame):
    global five_oclock
    print ("Caught sigint")
    five_oclock = True

signal.signal(signal.SIGALRM, on_timeout)
signal.signal(signal.SIGINT, on_int)

def hsk_harder(dest, cmd, data = None, timeout = 1, max_tries = 3):
    pkt = None
    ntimeout = 0
#    time.sleep(0.1)
    global five_oclock
    while pkt is None and not five_oclock:
        hsk.send(HskPacket(dest, cmd, data))
        try:
            signal.alarm(timeout)
            pkt = hsk.receive()
            if pkt is None:
                print("WTF: %d %d\n" %( dest, cmd))
            signal.alarm(0)
        except: 
            ntimeout+=1
            if ntimeout == max_tries:
                print( "Giving up on " + str((dest,cmd)) + " after %d attempts " % (max_tries))
                return None
            print( str((dest,cmd)) + " timed out %d times trying again" % ( ntimeout))
            pkt = None


    return pkt

turfios = (0x40,0x48,0x50,0x58)
surfs = (0x93, 0x9b, 0x96, 0x8e, 0x90, 0x8f, 0x89, 0x88, 0x9e, 0x8b, 0xa1, 0x98, 0x97, 0xa0, 0x99, 0x8d, 0x9d,0x94, 0x8a, 0x8c, 0x95, 0x9f, 0x9a, 0x87, 0x85, 0x9c )
print(" Number of SURFS I know: %d" % len(surfs))

if hsk_harder(0x60,'ePingPong') is None:
    print ("ruh roh, can't ping TURF")
    sys.exit(1)

for tfio in turfios:
    if hsk_harder(tfio, 'ePingPong') is None:
        print ("ruh roh can't ping TURFIO " + hex(tfio))

if hsk_harder(surfs[3], 'ePingPong') is None:
    print("Can't ping a surf, let's try to enable them all")
    for tfio in turfios:
        print(hsk_harder(tfio,'eEnable',(0x40,0x40)))

    # connect to database

psql = None
if 'BEMCO_CONNINFO' in os.environ: 
    psql = psycopg2.connect(os.environ['BEMCO_CONNINFO'])
    psql.autocommit = True
else:
    print("No psql conninfo, sorry")

eye_counter = 0

while not five_oclock:

    devs = (0x60,) + turfios + surfs
    print('------')
    print(time.ctime())
    temps = {}

    for dev in devs:
        T = hsk_harder(dev,'eTemps')
        if T is not None:
            d = T.prettyDict()
            if d is not None:
                for k in d.keys():
                    temps[k] = d[k]
            else:
                print("no pretty dict for %s" % (d.pretty()))

        if five_oclock:
            break


    print (temps)

    if psql is not None:
        with psql.cursor() as curs:
            for k in temps.keys():
                curs.execute("INSERT INTO temperatures (time, device, sensor, temperature) values (NOW(), 'DAQ', '%s', %f);" % (k, temps[k]))


    if (eye_counter % 60 == 0 and not five_oclock):
        eye_gbe = hsk_harder(0x60,0x14,[0])
        eye_tfio = hsk_harder(0x60,0x14,[1])

        print("gbe eye")
        print(eye_gbe.pretty())
        print("tfio eye")
        print(eye_tfio.pretty())
        with open('eyes/eye_tfio_%s' % (time.strftime('%Y%m%d_%H%M%S')),'w') as feye:
            feye.write(eye_tfio.pretty())
        with open('eyes/eye_gbe_%s' % (time.strftime('%Y%m%d_%H%M%S')),'w') as feye:
            feye.write(eye_gbe.pretty())


    eye_counter +=1


    time.sleep(10)


