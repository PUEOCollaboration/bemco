#! /usr/bin/env python3

from pueo.turf import PueoTURF
import time
from pueo.turfio import PueoTURFIO

turf = PueoTURF(None, 'Ethernet')
for link in range(4):
    print("Turning on link %d" % (link))
    try:
       dev = PueoTURFIO((turf, link), 'TURFGTP') 
       dev.program_sysclk(dev.ClockSource.TURF) 
       dev.enable_rxclk(True)

    except:
        print("Couldn't do it")

    time.sleep(5)


