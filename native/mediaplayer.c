#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "libavformat/avformat.h"  
#include "libavcodec/avcodec.h"
#include "libavutil/time.h"  
#include "libavutil/mathematics.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "MediaPlayer.h"

pthread_mutex_t mutex;

#ifdef WINDOWS
#include <sys/time.h>
pthread_cond_t cond;
struct timespec wait;
#endif

#define MPI_MAX 16
#define BUF_MAX 5

typedef struct FrameItem {
        AVFrame *avf;
        struct FrameItem *next;
}FrameItem;

typedef struct FrameQ {
		FrameItem	*head;
		FrameItem	*tail;
		int  	count;
		sem_t	fqsem;
}FrameQ;

void FQInit(struct FrameQ *q)
{
	q->head = NULL;
	q->tail = NULL;
	q->count = 0;
	sem_init(&q->fqsem,0,1);
}
void FQClean(struct FrameQ *q)
{
        q->head = NULL;
        q->tail = NULL;
        q->count = 0;
        sem_destroy(&q->fqsem);
}
void FQPush(struct FrameQ *q, FrameItem *f)
{
        sem_wait(&q->fqsem);
        if(q->count==0){
                f->next = NULL;
                q->head = f;
                q->tail = f;
                q->count++;
        }else{
                f->next = NULL;
                q->tail->next = f;
                q->tail = f;
                q->count++;
        }
        sem_post(&q->fqsem);
}

FrameItem *FQPop(struct FrameQ *q)
{
        FrameItem *ret = NULL;

        sem_wait(&q->fqsem);
        if(q->count > 0){
                ret = q->head;
                q->head = q->head->next;
                q->count--;
        }
        sem_post(&q->fqsem);

        return ret; 
}

typedef struct MPInstance{
        FrameQ 	wQ;
        FrameQ 	rQ;
        FrameItem	FI[BUF_MAX];
        int 	width;
        int 	height;
        char 	url[128];
        int 	state;
        pthread_t	tid;
} MPInstance;
MPInstance mpi[MPI_MAX]; 
int mpi_count = 0;

/**********************************
  return the media player instance id
 **********************************/
int get_mpi_id(void)
{
        int mpi_id = -1;
        int i;

        if(mpi_count<MPI_MAX){
                for(i=0; i<MPI_MAX; i++){
                        if(mpi[i].state==0){
                                mpi_id = i;
                                mpi_count++;
                                break;
                        }
                }
        }

        return mpi_id;
}

void ff_init(void)
{
        memset(mpi,0,sizeof(mpi));
        pthread_mutex_init(&mutex,NULL);
#ifdef WINDOWS
        pthread_cond_init(&cond,NULL);
#endif
        av_register_all();  
        avformat_network_init(); 
}
void ff_exit(void)
{
        pthread_mutex_destroy(&mutex);
#ifdef WINDOWS
        pthread_cond_destroy(&cond);
#endif
}

void *do_ff_open(void *arg)
{
        int i;
        MPInstance *pmpi = (MPInstance *)arg;

        printf("open url=%s\n",pmpi->url);

        AVFormatContext *pFormatCtx = avformat_alloc_context();  
	
	AVDictionary *format_opts = NULL;
	av_dict_set(&format_opts,"rw_timeout","5000000",0);

        if(avformat_open_input(&pFormatCtx, pmpi->url, NULL,&format_opts)!=0){  
                perror("avformat_open_input():");
                goto exit_1; 
        }
#ifdef WINDOWS
	pthread_cond_signal(&cond);
#endif
        //pFormatCtx->flags |= AVFMT_FLAG_NOBUFFER; 
        pFormatCtx->max_analyze_duration = AV_TIME_BASE;

        if(avformat_find_stream_info(pFormatCtx,NULL)<0){  
                perror("avformat_find_stream_info():");
                goto exit_1;
        }  
        int videoindex=-1;  
        for(i=0; i<pFormatCtx->nb_streams; i++)   
                if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO){  
                        videoindex=i;  
                        break;  
                }  
        if(videoindex==-1){  
                printf("Couldn't find a video stream.\n");  
                pthread_exit(NULL); 
        }

        AVCodecContext *pCodecCtx = pFormatCtx->streams[videoindex]->codec;  
        AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);  
        if(pCodec==NULL){  
                perror("avcodec_find_decoder():");
                goto exit_1;
        }  

        if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){  
                perror("avcodec_open2():");  
                goto exit_2;
        }  

        AVPacket *packet=(AVPacket *)av_malloc(sizeof(AVPacket));  
        AVFrame *pFrame = av_frame_alloc();
        FQInit(&pmpi->wQ);
        FQInit(&pmpi->rQ);

        for(i=0;i<BUF_MAX;i++){ 
                pmpi->FI[i].avf = av_frame_alloc(); 
                pmpi->FI[i].next = NULL;
                int packet_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pCodecCtx->width, pCodecCtx->height,1);
                uint8_t *out_buffer = (uint8_t *)av_malloc(packet_size);  
                av_image_fill_arrays(pmpi->FI[i].avf->data, pmpi->FI[i].avf->linesize,out_buffer,AV_PIX_FMT_RGBA,pCodecCtx->width,pCodecCtx->height,1);  
                FQPush(&pmpi->wQ,&pmpi->FI[i]);
        }

        struct SwsContext *img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,   
                        pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL, NULL);   
        if(img_convert_ctx==NULL){
                perror("sws_getContext():");
        }

        pmpi->height = pCodecCtx->height;
        pmpi->width = pCodecCtx->width;
        int got_picture,ret;  
        while(pmpi->state==1 || pmpi->state==2){  //0->1->2 = close->open->decode 
                if(av_read_frame(pFormatCtx, packet)>=0 && packet->stream_index==videoindex){  
                        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);  
                        if(ret < 0){  
                                perror("avcodec_decode_video2():");
                                goto exit_3;  
                        } 
                        if(got_picture){
                                FrameItem *fip = FQPop(&pmpi->wQ);//get a vaild buffer from write queue
                                if(fip==NULL){//if there is no available buffer in write queue
                                        fip = FQPop(&pmpi->rQ);//fetch buffer from the read queue, which means frame lost
                                }
                                sws_scale(img_convert_ctx, (const uint8_t * const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, 
                                                fip->avf->data, fip->avf->linesize); 
                                FQPush(&pmpi->rQ,fip);//push the rgb data buffer to the read queue

                                if(pmpi->state==1)
                                        pmpi->state = 2;
                        }  
                }  
                av_free_packet(packet);  
        }  
exit_3:
        for(i=0;i<BUF_MAX;i++){
                av_frame_free(&pmpi->FI[i].avf);
        }
        FQClean(&pmpi->wQ);
        FQClean(&pmpi->rQ);
        av_frame_free(&pFrame);  
        sws_freeContext(img_convert_ctx);  
exit_2:
        avcodec_close(pCodecCtx);
exit_1:  
        avformat_close_input(&pFormatCtx);

        return arg; 
}
/**********************************
return : mpi_id success ,-1 error
 ***********************************/
int ff_open(char *url, int len)
{
        if(len>127){
                return -1;
        }

        pthread_mutex_lock(&mutex);

        int mpi_id = get_mpi_id();
        if(mpi_id==-1){
                printf("stream instance reached the max\n");
                pthread_mutex_unlock(&mutex);
                return -1;
        }

        mpi[mpi_id].state = 1;
        strncpy(mpi[mpi_id].url, url,len);
        mpi[mpi_id].url[len]='\0';

        if(0!=pthread_create(&mpi[mpi_id].tid,NULL,do_ff_open,&mpi[mpi_id])){
                perror("pthread_create():");
                mpi[mpi_id].state = 0;
                pthread_mutex_unlock(&mutex);
                return -1;
        } 
#ifdef WINDOWS
	struct timeval now;
	gettimeofday(&now, NULL);
	wait.tv_sec = now.tv_sec + 2;
	wait.tv_nsec = now.tv_usec * 1000;
	pthread_cond_timedwait(&cond,&mutex,&wait);
#endif
        
        pthread_mutex_unlock(&mutex);
        return mpi_id;    
}

int ff_close(int mpi_id)
{
        mpi[mpi_id].state = 3;//exit the decoding thread
        pthread_join(mpi[mpi_id].tid,NULL);

        pthread_mutex_lock(&mutex);
        mpi_count--;
        memset(&mpi[mpi_id],0,sizeof(mpi[mpi_id]));
        pthread_mutex_unlock(&mutex);

        return 1;
}

int ff_getframe(int mpi_id,unsigned char *buffer)
{
        if(mpi[mpi_id].state != 2)
                return -1;

        FrameItem *fip = FQPop(&mpi[mpi_id].rQ); //get rgb data from the read queue
        if(fip!=NULL){
                memcpy(buffer,fip->avf->data[0],4*mpi[mpi_id].height*mpi[mpi_id].width);
                FQPush(&mpi[mpi_id].wQ,fip);//push the buffer to write queue after processed 
        }else{
                return -1;
        }
        return 1; 
}

JNIEXPORT jint JNICALL Java_MediaPlayer_init(JNIEnv *env, jobject obj)
{
        ff_init();
        return 1;
}

JNIEXPORT jint JNICALL Java_MediaPlayer_exit(JNIEnv *env, jobject obj)
{
        ff_exit();
        return 1;
}

JNIEXPORT jint JNICALL Java_MediaPlayer_open(JNIEnv *env, jobject obj,jbyteArray arr, jint len)
{
        char *url = (char*)(*env)->GetByteArrayElements(env,arr,NULL);
        int ret = -1; 
        ret = ff_open(url,len);
        (*env)->ReleaseByteArrayElements(env,arr,url,0);

        return ret;
}
JNIEXPORT jint JNICALL Java_MediaPlayer_close(JNIEnv *env, jobject obj, jint id)
{
        int ret = -1;
        ret = ff_close(id);
        return ret;
}

JNIEXPORT jint JNICALL Java_MediaPlayer_getwidth(JNIEnv *env, jobject obj, jint id)
{
        if(mpi[id].state==2)
                return mpi[id].width;
        else 
                return 0;
}

JNIEXPORT jint JNICALL Java_MediaPlayer_getheight(JNIEnv *env, jobject obj, jint id)
{
        if(mpi[id].state==2)
                return mpi[id].height;
        else 
                return 0;
}

JNIEXPORT jint JNICALL Java_MediaPlayer_getframe(JNIEnv *env, jobject obj, jint id, jbyteArray buffer)
{
        int ret = -1;
        unsigned char *pbuffer = (unsigned char *)(*env)->GetByteArrayElements(env,buffer,NULL);
        if(pbuffer!=NULL){
                ret = ff_getframe(id,pbuffer);
        }
        (*env)->ReleaseByteArrayElements(env,buffer,pbuffer,0);

        return ret;
}



