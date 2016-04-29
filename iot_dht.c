#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"
#include "profile.h"
#include <time.h>
#include<dirent.h>

#define BUF_LEN  1024
#define ADDR_LEN 100
#define MAX_TIME 100

int dht22_val[5]={0,0,0,0,0};
char g_strbuf[BUF_LEN]={0};
int g_dht22_pin = 0;
int  g_led_pin = 2;
int g_led_status = -1;
char g_ledctl_url[ADDR_LEN] = {0};
char g_upload_url[ADDR_LEN] = {0};
 
time_t now;
struct tm *timenow;

int g_write_line = 0;
char g_filepath[ADDR_LEN] = {0};

char* get_json_string(float ft,float fh)
{
	cJSON *root;
	root=cJSON_CreateObject();//创建项目
	cJSON_AddNumberToObject(root,"temperature", ft);
	cJSON_AddNumberToObject(root,"humidity", fh);
	char* out = cJSON_Print(root); 
	cJSON_Delete(root);
	printf("request:\n%s\n",out);
	//free(out);
	return out;
}

size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
    if (strlen((char *)stream) + strlen((char *)ptr) > BUF_LEN) return 0;
    strcat(stream, (char *)ptr);
    return size*nmemb;
}

int post_request(char *url,char* str)
{
    strcpy(g_strbuf,""); 
    
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    
    if (curl) {
	struct curl_slist *headers=NULL; // init to NULL is important
   	headers = curl_slist_append(headers, "Accept: application/json");
   	headers = curl_slist_append( headers, "Content-Type: application/json");
	headers = curl_slist_append( headers, "charsets: utf-8");
	
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        //curl_easy_setopt(curl, CURLOPT_PUT,1);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);//设置超时时间
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);//设置写数据的函数
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, g_strbuf);//设置写数据的变量
        res = curl_easy_perform(curl);
        
	curl_slist_free_all(headers);
	
	curl_easy_cleanup(curl);
        curl = NULL; 

        if (CURLE_OK == res)
        {
	    time(&now);
            timenow = localtime(&now);
            printf("%s\n",asctime(timenow));
            printf("response:\n%s",g_strbuf);
            return 0;
        }
    }

    return -1;
}

int dht22_read_val(){
    uint8_t lststate=HIGH;         //last state
    uint8_t counter=0;
    uint8_t j=0,i;
    for(i=0;i<5;i++)
        dht22_val[i]=0;         
    //host send start signal    
    pinMode(g_dht22_pin,OUTPUT);      //set pin to output
    digitalWrite(g_dht22_pin,LOW);    //set to low at least 18ms
    delay(18);
    digitalWrite(g_dht22_pin,HIGH);   //set to high 20-40us
    delayMicroseconds(30);
     
    //start recieve dht response
    pinMode(g_dht22_pin,INPUT);       //set pin to input
    for(i=0;i<MAX_TIME;i++)         
    {
        counter=0;
        while(digitalRead(g_dht22_pin)==lststate){     //read pin state to see if dht responsed. if dht always high for 255 + 1 times, break this while circle
            counter++;
            delayMicroseconds(1);
            if(counter==255)
                break;
        }
        lststate=digitalRead(g_dht22_pin);             //read current state and store as last state.
        if(counter==255)                            //if dht always high for 255 + 1 times, break this for circle
            break;
        // top 3 transistions are ignored, maybe aim to wait for dht finish response signal
        if((i>=4)&&(i%2==0)){
            dht22_val[j/8]<<=1;                     //write 1 bit to 0 by moving left (auto add 0)
            if(counter>16)                          //long mean 1
                dht22_val[j/8]|=1;                  //write 1 bit to 1 
            j++;
        }
    }
    // verify checksum and print the verified data
    if((j>=40)&&(dht22_val[4]==((dht22_val[0]+dht22_val[1]+dht22_val[2]+dht22_val[3])& 0xFF)))
    {
        float f, h;
          h = dht22_val[0] * 256 + dht22_val[1];
          h /= 10;
          f = (dht22_val[2] & 0x7F)* 256 + dht22_val[3];
        f /= 10.0;
        if (dht22_val[2] & 0x80) 
		 f *= -1;
        
        printf("Temp =  %.1f *C, Hum = %.1f \%\n", f, h);
	FILE *fp=fopen(g_filepath,"a");
	if (fp)  // 返回结果用文件存储
        {
		char buffer[100]={0};
		sprintf( buffer,"%d Send [IR_PPG1468, HeartBeat:%.1f, SugarBlood:%.1f]\n",++g_write_line,f,h);
		fwrite(buffer,strlen(buffer),1,fp);
		fclose(fp);
	}

	char* str = get_json_string(f,h);
	    
	i=0;
	int ret = -1;
	while(i<20)
	{
		ret = post_request(g_upload_url,str);
		if(ret != 0)
		{
			sleep(1);
			continue;
		}
		else
		{
			break;
		}
	}
	
        free(str);
        
        return ret;
    }
    else
    {
	printf("get HT data fail!\n");
        return -1;
    }
}

int parseJson(char * pMsg)
{
    if(NULL == pMsg)
    {
        return -1;
    }
    cJSON * pJson = cJSON_Parse(pMsg);
    if(NULL == pJson)
    {
        // parse faild, return
        return -1;
    }
    
    int ret = -1;
    int iSize = cJSON_GetArraySize(pJson);
    int iCnt = 0;
    for(iCnt = 0; iCnt < iSize; iCnt++)
    {
        cJSON * pSub = cJSON_GetArrayItem(pJson, iCnt);
        if(NULL == pSub)
        {
            continue;
        }
        
        cJSON * pValue = cJSON_GetObjectItem(pSub, "value");
    	if(NULL == pValue)
    	{
            //get object named "value" faild
            break; 
    	}
        ret = pValue->valueint;
        //printf("value[%2d] : [%d]\n", iCnt, ret);
	break;
    }   
    
    cJSON_Delete(pJson);
    
    return ret;
}

int led_display()
{
    strcpy(g_strbuf,"");

    CURL *curl;
    CURLcode res;
    
    curl = curl_easy_init();

    if (curl) {
        struct curl_slist *headers=NULL; // init to NULL is important
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append( headers, "Content-Type: application/json");
        headers = curl_slist_append( headers, "charsets: utf-8");
        
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, g_ledctl_url);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);//设置超时时间
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);//设置写数据的函数
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, g_strbuf);//设置写数据的变量
        res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
        curl = NULL; 
	//printf("get led status return %d\n",res);
        if (CURLE_OK == res)
        {
            //printf("response:\n%s",g_strbuf);
            int ctl = parseJson(g_strbuf);
            if (ctl != g_led_status) {
		time(&now);
           	timenow = localtime(&now);
           	printf("%s\n",asctime(timenow));
		printf("response:\n%s",g_strbuf);
		g_led_status = ctl;
                pinMode(g_led_pin,OUTPUT);
                digitalWrite(g_led_pin, ctl);
            }
            return 0;
        }
	else
	{
		printf("get led status return %d\n",res);
	}
    }

    return -1;
}

int get_conf(char* filename)
{
    //char filename[ADDR_LEN];
    //getcwd(filename,sizeof(filename));
    //strcat(filename,"/dht.conf");
    char temp[ADDR_LEN] = {0};
    int ret = GetProfileString(filename, "server", "addr", temp);
    if (ret != 0)
    {
        printf("read server addr fail!");
        return -1;
    }
    
    if (temp[strlen(temp) - 1] != '/') {
        temp[strlen(temp)] = '/';
    }
    //printf("server addr=%s\n",temp);
    memcpy(g_upload_url,temp,strlen(temp));
    strcat(g_upload_url,"api/comfort-meter/");
    memcpy(g_ledctl_url,temp,strlen(temp));
    strcat(g_ledctl_url,"api/light-bulb/state/");
    //printf("upload_url=%s,ledctl_url=%s\n",g_upload_url,g_ledctl_url);
    
    memset(temp,ADDR_LEN,0);
    ret = GetProfileString(filename, "gpiopin", "dht22", temp);
    if (ret != 0)
    {
        printf("read dht22 pin fail,use default 0\n");
    }
    else
    {
        g_dht22_pin = atoi(temp);
    }
    
    memset(temp,ADDR_LEN,0);
    ret = GetProfileString(filename, "gpiopin", "led", temp);
    if (ret != 0)
    {
        printf("read led pin fail,use default 2\n");
    }
    else
    {
        g_led_pin = atoi(temp);
    }
    
    ret = GetProfileString(filename, "server", "localfilepath", g_filepath);
    if (ret != 0)
    {
        printf("read server filesavepath fail!");
       // return -1;
	char* defaultpath="/home/pi/iot/UHealthServer_RaspberryPi/assets/";
	memcpy(g_filepath,defaultpath,strlen(defaultpath));
    }

    if (g_filepath[strlen(g_filepath) - 1] != '/') {
        g_filepath[strlen(g_filepath)] = '/';
    }
    	if(NULL==opendir(g_filepath))
	{
		char cmd[100]={0};
		sprintf(cmd,"mkdir -p %s",g_filepath);
		system(cmd);
	}
    strcat(g_filepath,"measurment.txt");
    return 0;
    
}

int main(void)
{
    if (get_conf("/home/pi/dht.conf") == -1) {
        exit(1);
    }
    
    if(wiringPiSetup()==-1)
        exit(1);
    
    int counter = 0;
    int ret = -1;
    while(1)
    {
        led_display();fflush(stdout); 
        if ( (ret != 0)||(counter%500 == 0))
        {
            ret = dht22_read_val();
            counter = 1;
        }
        //else
        {
            sleep(1);
            counter++;
        }
    }

    return 0;
}
