/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* values */
volatile int timerexpired=0;
int speed=0;
int success=0;
int failed=0;
int bytes=0;
float totalTime = 0;
float minTime = 0;
float maxTime = 0;
/* globals */
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE, POST */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define METHOD_POST 4
#define PROGRAM_VERSION "1.8"
int method=METHOD_GET;
int clients=1;
int force=0;
int force_reload=0;
int proxyport=80;
char *proxyhost=NULL;
int benchtime=30;
/* internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];
#define POSTDATA_SIZE 1024
char post_data[POSTDATA_SIZE] = {0};
char post_data_len[5] = {0};

/* multiple postdata */
char *postdataall = NULL;
int postdataallline = 0;
char *requestall = NULL;
int requestallsize = 0;

static const struct option long_options[]=
{
 {"force",no_argument,&force,1},
 {"reload",no_argument,&force_reload,1},
 {"time",required_argument,NULL,'t'},
 {"help",no_argument,NULL,'?'},
 {"http09",no_argument,NULL,'9'},
 {"http10",no_argument,NULL,'1'},
 {"http11",no_argument,NULL,'2'},
 {"get",no_argument,&method,METHOD_GET},
 {"head",no_argument,&method,METHOD_HEAD},
 {"options",no_argument,&method,METHOD_OPTIONS},
 {"trace",no_argument,&method,METHOD_TRACE},
 {"post",required_argument,NULL,'P'},
 {"file",required_argument,NULL,'F'},
 {"version",no_argument,NULL,'V'},
 {"proxy",required_argument,NULL,'p'},
 {"clients",required_argument,NULL,'c'},
 {NULL,0,NULL,0}
};

/* prototypes */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

static void alarm_handler()
{
   timerexpired=1;
}	

static void usage(void)
{
   fprintf(stderr,
	"webbench [option]... URL\n"
	"  -f|--force               Don't wait for reply from server.\n"
	"  -r|--reload              Send reload request - Pragma: no-cache.\n"
	"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	"  -p|--proxy <server:port> Use proxy server for request.\n"
	"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	"  -9|--http09              Use HTTP/0.9 style requests.\n"
	"  -1|--http10              Use HTTP/1.0 protocol.\n"
	"  -2|--http11              Use HTTP/1.1 protocol.\n"
	"  --get                    Use GET request method.\n"
	"  --head                   Use HEAD request method.\n"
	"  --options                Use OPTIONS request method.\n"
	"  --trace                  Use TRACE request method.\n"
	"  -P|--post                Use POST request method.\n"
	"  -F|--file                Use POST request method.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
	);
};

int get_datafromfile(char *filename);

int main(int argc, char *argv[])
{
 int opt=0;
 int options_index=0;
 char *tmp=NULL;

 if(argc==1)
 {
	  usage();
      return 2;
 } 

 while((opt=getopt_long(argc,argv,"912Vfrt:p:P:F:c:?h",long_options,&options_index))!=EOF )
 {
  switch(opt)
  {
   case  0 : break;
   case 'f': force=1;break;
   case 'r': force_reload=1;break; 
   case '9': http10=0;break;
   case '1': http10=1;break;
   case '2': http10=2;break;
   case 'V': printf(PROGRAM_VERSION"\n");exit(0);
   case 't': benchtime=atoi(optarg);break;	     
   case 'p': 
	     /* proxy server parsing server:port */
	     tmp=strrchr(optarg,':');
	     proxyhost=optarg;
	     if(tmp==NULL)
	     {
		     break;
	     }
	     if(tmp==optarg)
	     {
		     fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
		     return 2;
	     }
	     if(tmp==optarg+strlen(optarg)-1)
	     {
		     fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
		     return 2;
	     }
	     *tmp='\0';
	     proxyport=atoi(tmp+1);break;
   case 'P':
   	{
   		 snprintf(post_data, sizeof(post_data), "%s", optarg);
   		 snprintf(post_data_len, sizeof(post_data_len), "%zu", strlen(post_data));
   		 method = METHOD_POST;
   		 break;
   	}
   case 'F':
    {
   		 if(get_datafromfile(optarg))
   		 	return 3;
   		 method = METHOD_POST;
   		 break;
   	}	 
   case ':':
   case 'h':
   case '?': usage();return 2;break;
   case 'c': clients=atoi(optarg);break;
  }
 }
 
 if(optind==argc) {
                      fprintf(stderr,"webbench: Missing URL!\n");
		      usage();
		      return 2;
                    }

 if(clients==0) clients=1;
 if(benchtime==0) benchtime=60;
 /* Copyright */
 fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
	 "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	 );
 build_request(argv[optind]);
 /* print bench info */
 printf("\nBenchmarking: ");
 switch(method)
 {
	 case METHOD_GET:
	 default:
		 printf("GET");break;
	 case METHOD_OPTIONS:
		 printf("OPTIONS");break;
	 case METHOD_HEAD:
		 printf("HEAD");break;
	 case METHOD_TRACE:
		 printf("TRACE");break;
	 case METHOD_POST:
	 	 printf("POST");break;
 }
 printf(" %s",argv[optind]);
 switch(http10)
 {
	 case 0: printf(" (using HTTP/0.9)");break;
	 case 2: printf(" (using HTTP/1.1)");break;
 }
 printf("\n");
 if(clients==1) printf("1 client");
 else
   printf("%d clients",clients);

 printf(", running %d sec", benchtime);
 if(force) printf(", early socket close");
 if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
 if(force_reload) printf(", forcing reload");
 printf(".\n");
 return bench();
}

int urlencode(char *src, int srclen, char *dst, int dstlen)
{
  int i, j = 0;
  char ch;
  for(i = 0;i < srclen && j < dstlen; i++)
  {
  	ch = src[i];
  	if((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
  		dst[j++] = ch;
  	else if(' ' == ch)
  		dst[j++] = '+';
  	else if('.' == ch || '-' == ch || '_' == ch || '*' == ch || '=' == ch)
  		dst[j++] = ch;
  	else{
  		if(j + 3 < dstlen)
  		{
  			snprintf(dst + j, dstlen - j, "%%%02X", (unsigned char)ch);
  			j += 3;
  		}
  		else
  			return -1;
  	}
  }
  dst[j] = '\0';
  return 0;
}

/*int urlencodeall(char *src, int srclen, char *dst, int dstlen)
{
	return 0;
}*/

int get_datafromfile(char *filename)
{
	int fd, offset, line, maxsize;
	struct stat st;
	void *ptr;
	char *ch, *br = NULL;
	
	fd = open(filename, O_RDONLY);
	if(-1 == fd)
		return -1;

	if(-1 == fstat(fd, &st) || 0 == st.st_size)
		return -1;

	
	ptr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if(!ptr)
		return -1;

	ch = (char *)ptr;
	for(maxsize = 0, line = 0, offset = 0; offset < st.st_size; offset++)
	{
		ch++;
		if(offset > 0 && *ch == '\n')
		{
			if(!br) 
				maxsize = offset;
			else if(maxsize < ch - br)
				maxsize = ch - br;
			br = ch;
			line++;
		}
	}

	if(line < 1 && line > 1000)
		return -1;
	
	//printf("line : %d, maxsize : %d\n", line, maxsize);

	int n = 0;
	char *data = calloc(POSTDATA_SIZE, line);
	if(!data)
		return -1;

	ch = (char *)ptr;
	br = NULL;
	for(offset = 0; offset < st.st_size; offset++)
	{
		ch++;
		if(offset > 0 && *ch == '\n')
		{
			if(!br){ 
				if(offset > 1)
				{
					memcpy(data, ch-offset, offset);
					*(data+offset-1) = '\0';
					n++;
				}
			}
			else
			{
				if((ch - br) > 1)
				{
					memcpy(data+n*POSTDATA_SIZE, br+1, ch-br);
					*(data+n*POSTDATA_SIZE+(ch-br)-1) = '\0';
					n++;
				}
			}
			br = ch;
		}
	}

//	int i;
//	for(i = 0; i < n; i++)
//	{
//		printf("%s\n", data+i*maxsize);
//	}
	
	munmap(ptr, st.st_size);
	close(fd);

	postdataall = data;
	postdataallline = n;

	return 0;
}

void build_request(const char *url)
{
  char tmp[10];
  int i;

  bzero(host,MAXHOSTNAMELEN);
  bzero(request,REQUEST_SIZE);

  if(force_reload && proxyhost!=NULL && http10<1) http10=1;
  if(method==METHOD_HEAD && http10<1) http10=1;
  if(method==METHOD_OPTIONS && http10<2) http10=2;
  if(method==METHOD_TRACE && http10<2) http10=2;
  if(method==METHOD_POST && http10<2) http10=2;

  switch(method)
  {
	  default:
	  case METHOD_GET: strcpy(request,"GET");break;
	  case METHOD_HEAD: strcpy(request,"HEAD");break;
	  case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
	  case METHOD_TRACE: strcpy(request,"TRACE");break;
	  case METHOD_POST: strcpy(request,"POST");break;
  }
		  
  strcat(request," ");

  if(NULL==strstr(url,"://"))
  {
	  fprintf(stderr, "\n%s: is not a valid URL.\n",url);
	  exit(2);
  }
  if(strlen(url)>1500)
  {
         fprintf(stderr,"URL is too long.\n");
	 exit(2);
  }
  if(proxyhost==NULL)
	   if (0!=strncasecmp("http://",url,7)) 
	   { fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
             exit(2);
           }
  /* protocol/host delimiter */
  i=strstr(url,"://")-url+3;
  /* printf("%d\n",i); */
  //printf("url+i = %s\n",(url+i));
  if(strchr(url+i,'/')==NULL) {
                                fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
                                exit(2);
                              }
  if(proxyhost==NULL)
  {
   /* get port from hostname */
   if(index(url+i,':')!=NULL &&
      index(url+i,':')<index(url+i,'/'))
   {
	   strncpy(host,url+i,strchr(url+i,':')-url-i);
	   bzero(tmp,10);
	   strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
	   /* printf("tmp=%s\n",tmp); */
	   proxyport=atoi(tmp);
	   if(proxyport==0) proxyport=80;
   } else
   {
     strncpy(host,url+i,strcspn(url+i,"/"));
   }
   // printf("Host=%s\n",host);
   strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
  } else
  {
   // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
   strcat(request,url);
  }
  if(http10==1)
	  strcat(request," HTTP/1.0");
  else if (http10==2)
	  strcat(request," HTTP/1.1");
  strcat(request,"\r\n");
  if(http10>0)
	  strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
  if(proxyhost==NULL && http10>0)
  {
	  strcat(request,"Host: ");
	  strcat(request,host);
	  strcat(request,"\r\n");
  }
  if(force_reload && proxyhost!=NULL)
  {
	  strcat(request,"Pragma: no-cache\r\n");
  }
  if(http10>1)
	  strcat(request,"Connection: close\r\n");
  /* add post data */
  if(method==METHOD_POST)
  {
		if(postdataall)
		{
			requestall = calloc(POSTDATA_SIZE, postdataallline);
			if(!requestall)
				return;

			for(i = 0; i < postdataallline; i++)
			{
				snprintf(requestall+i*POSTDATA_SIZE, POSTDATA_SIZE,
								"%s"
								"Accept: */*\r\n"
								"Content-Length: "
								"%zu"
								"\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n"
								"%s",
								request,
								strlen(postdataall+i*POSTDATA_SIZE),
								postdataall+i*POSTDATA_SIZE);
			}
			requestallsize = i;
			clients = i < clients ? i : clients;
		}
		else
		{
			strcat(request, "Accept: */*\r\n");
			strcat(request, "Content-Length: ");
			strcat(request, post_data_len);
			strcat(request, "\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n");
			strcat(request, post_data);
		}
  }
  /* add empty line at end */
  if(http10>0) strcat(request,"\r\n"); 
  printf("%s\n",request);
}

/* vraci system rc error kod */
static int bench(void)
{
  int i,j,k,x;
  float l,m,n;
  pid_t pid=0;
  FILE *f;

  /* check avaibility of target server */
  i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
  if(i<0) { 
	   fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
           return 1;
         }
  close(i);
  /* create pipe */
  if(pipe(mypipe))
  {
	  perror("pipe failed.");
	  return 3;
  }

  /* not needed, since we have alarm() in childrens */
  /* wait 4 next system clock tick */
  /*
  cas=time(NULL);
  while(time(NULL)==cas)
        sched_yield();
  */

  /* fork childs */
  for(i=0;i<clients;i++)
  {
	   pid=fork();
	   if(pid <= (pid_t) 0)
	   {
		   /* printf("%d child %4d %4d %4d\n", i, getppid(),getpid(), pid); */
		   /* child process or error*/
	       sleep(1); /* make childs faster */
		   break;
	   }
  }

  if( pid< (pid_t) 0)
  {
      fprintf(stderr,"problems forking worker no. %d\n",i);
	  perror("fork failed.");
	  return 3;
  }

  if(pid== (pid_t) 0)
  {
    /* I am a child */
    //printf("child doing\n");
    if(proxyhost==NULL)
      benchcore(host,proxyport,request);
         else
      benchcore(proxyhost,proxyport,request);

     /* write results to pipe */
	 f=fdopen(mypipe[1],"w");
	 if(f==NULL)
	 {
		 perror("open pipe for writing failed.");
		 return 3;
	 }
	 /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
	 fprintf(f,"%d %d %f %f %f %d %d\n",speed,success,totalTime,minTime,maxTime,failed,bytes);
	 fclose(f);
	 return 0;
  } else
  {
	  //printf("father doing\n");
	  f=fdopen(mypipe[0],"r");
	  if(f==NULL) 
	  {
		  perror("open pipe for reading failed.");
		  return 3;
	  }
	  setvbuf(f,NULL,_IONBF,0);
	  speed=0;
	  success=0;
      failed=0;
      bytes=0;
	  totalTime=0.0;
	  minTime = 0.0;
	  maxTime = 0.0;
	  while(1)
	  {
		  pid=fscanf(f,"%d %d %f %f %f %d %d",&i,&x,&l,&m,&n,&j,&k);
		  if(pid<2)
          {
               fprintf(stderr,"Some of our childrens died.\n");
               break;
          }
		  speed+=i;
		  success+=x;
		  totalTime+=l;
		  failed+=j;
		  bytes+=k;
		  if(minTime == 0.0 && maxTime == 0.0){
		  	minTime = m;
		  	maxTime = n;
		  }
		  minTime = m <= minTime ? m : minTime;
		  maxTime = n > maxTime ? n : maxTime;
		  /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
		  if(--clients==0) break;
	  }
	  fclose(f);

  printf("\nSpeed=%d requests, %d bytes/sec\nmin: %f second, average: %f second, max: %f second\nRequests: %d susceed, %d failed.\n",
		  speed,
		  (int)(bytes/(float)benchtime),
		  minTime,
		  speed > 0 ? (totalTime/speed) : 0,
		  maxTime,
		  success,
		  speed-success);
  }
  return i;
}

void benchcore(const char *host,const int port,const char *req)
{
 int rlen;
 char buf[16] = {'\0'};
 int s,i;
 struct sigaction sa;
 //float startTime = 0.0;
 float costTime = 0.0;

 /* setup alarm signal handler */
 sa.sa_handler=alarm_handler;
 sa.sa_flags=0;
 if(sigaction(SIGALRM,&sa,NULL))
    exit(3);
 alarm(benchtime);

 rlen=strlen(req);
 nexttry:while(1)
 {
    if(timerexpired)
    {
       if(failed>0)
       {
       	  speed--;
          /* fprintf(stderr,"Correcting failed by signal\n"); */
          failed--;
       }
       return;
    }
    struct timeval startTime,endTime;
    gettimeofday(&startTime,NULL);
    s=Socket(host,port);
    speed++;                    
    if(s<0) { failed++;continue;} 
    if(rlen!=write(s,req,rlen)) {failed++;close(s);continue;}
    if(http10==0) 
	    if(shutdown(s,1)) { failed++;close(s);continue;}
    if(force==0)
    {
            /* read all available data from socket */
	    while(1)
	    {
          if(timerexpired) break; 
	      i=read(s,buf,16);
          /* fprintf(stderr,"%d\n",i); */
	      if(i<0) 
          { 
             failed++;
             close(s);
             goto nexttry;
          }
	       else
		       if(i==0) break;
		       else{
		       	   if (strstr(buf, "200 OK") != NULL){
		       	   	  success++;
		       	   	  gettimeofday(&endTime,NULL);
		       	   	  costTime = (1000000 * ( endTime.tv_sec - startTime.tv_sec ) + endTime.tv_usec - startTime.tv_usec)/1000000.0;
		       	   	  totalTime += costTime;
		       	   	  if(minTime == 0.0 && maxTime == 0.0)
		       	   	  {
		       	   	  	minTime = costTime;
		       	   	  	maxTime = costTime;
		       	   	  }
		       	   	  maxTime = costTime > maxTime ? costTime : maxTime;
		       	   	  minTime = costTime <= minTime ? costTime : minTime;
		       	      break;
		       	   }
		       	   else{
		       	   	  failed++;
		       	   	  break;
		       	   }
			       //bytes+=i;
		       }
	    }
    }
    if(close(s)) {failed++;continue;}
 }
}
