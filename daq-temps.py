#! /usr/bin/env python3

import time
import psycopg2
import sys
import os
import signal

from HskSerial.HskSerial import HskEthernet, HskPacket

hsk = HskEthernet()

def on_timeout(signum,frame):
    raise TimeoutError()

signal.signal(signal.SIGALRM, on_timeout)

def hsk_harder(dest, cmd, data = None, timeout = 1, max_tries = 3):
    pkt = None
    ntimeout = 0
#    time.sleep(0.1)
    while pkt is None:
        hsk.send(HskPacket(dest, cmd, data))
        try: 
            signal.alarm(timeout)
            pkt = hsk.receive()
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
surfs = (0x93, 0x9b, 0x96, 0x8c, 0x90, 0x8f, 0x89, 0x88, 0x9e, 0x8b, 0xa1, 0x98, 0x97, 0xa0, 0x99, 0x8d, 0x9d,0x94, 0x8a, 0x8c, 0x95, 0x9f, 0x9a, 0x87, 0x85, 0x9c )

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


while True:

    devs = (0x60,) + turfios + surfs

    temps = {}

    for dev in devs:
        T = hsk_harder(dev,'eTemps')
        if T is not None:
            d = T.prettyDict()
            if d is not None:
                for k in d.keys():
                    temps[k] = d[k]


    print (temps)

    if psql is not None:
        with psql.cursor() as curs:
            for k in temps.keys():
                curs.execute("INSERT INTO temperatures (time, device, sensor, temperature) values (NOW(), 'DAQ', '%s', %f);" % (k, temps[k]))


    time.sleep(10)




