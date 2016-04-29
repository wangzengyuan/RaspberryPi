#!/bin/bash  
  
while true  
do   
    procnum=` ps -ef|grep "dht_sample"|grep -v grep|wc -l`  
   if [ $procnum -eq 0 ]; then  
       sudo /home/pi/dht_sample&  
   fi  
   sleep 30  
done 

