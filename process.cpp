//#include <android_camrec.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>
#include <semaphore.h>
#include <sys/prctl.h>

#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>

#include "prot.h"
#include <stdbool.h>

#include <queue>
using namespace std;

static int32_t sample_send_image(uint8_t devid);

extern volatile int force_exit;

int hostsock = -1;

void parse_cmd(uint8_t *buf, uint8_t *msgbuf);
static uint32_t unescaple_msg(uint8_t *buf, uint8_t *msg, int msglen);

sem_t send_data;

void sem_send_init()
{
    sem_init(&send_data, 0, 0);
}


int recv_ack = 0;
pthread_mutex_t  recv_ack_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t   recv_ack_cond = PTHREAD_COND_INITIALIZER;

int save_mp4 = 0;
pthread_mutex_t  save_mp4_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t   save_mp4_cond = PTHREAD_COND_INITIALIZER;

int GetFileSize(char *filename);

void notice_ack_msg()
{
    pthread_mutex_lock(&recv_ack_mutex);
    recv_ack = NOTICE_MSG;
    pthread_cond_signal(&recv_ack_cond);
    pthread_mutex_unlock(&recv_ack_mutex);
}


//实时数据处理
void RealTimeDdata_process(real_time_data *data, int mode)
{
    static real_time_data msg={0};
    static pthread_mutex_t real_time_msg_lock = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&real_time_msg_lock);
    if(mode == WRITE_REAL_TIME_MSG)
    {
        memcpy(&msg, data, sizeof(real_time_data));
    }
    else if(mode == READ_REAL_TIME_MSG)
    {
        memcpy(data, &msg, sizeof(real_time_data));
    }
    pthread_mutex_unlock(&real_time_msg_lock);
    
    printf("get car speed: %d\n", data->car_speed);
}

void get_latitude_info(char *buffer, int len)
{
    real_time_data tmp;
    RealTimeDdata_process(&tmp, READ_REAL_TIME_MSG);

    snprintf(buffer, len, " BD:%.6fN,%.6fE",\
            (MY_HTONL(tmp.latitude)*1.0)/1000000, (MY_HTONL(tmp.longitude)*1.0)/1000000);

    printf("latitude: %s\n", buffer);

}


#define WARNING_ID_MODE 0
#define MM_ID_MODE 1
//获取 报警ID，多媒体 ID
uint32_t get_next_id(int mode, uint32_t *id, uint32_t num)
{
    static pthread_mutex_t id_lock = PTHREAD_MUTEX_INITIALIZER;
    static uint32_t s_warning_id = 0;
    static uint32_t s_mm_id = 0;
    uint32_t warn_id = 0;
    uint32_t i;

    pthread_mutex_lock(&id_lock);
    if(mode == WARNING_ID_MODE)
    {
        s_warning_id ++;
        warn_id = s_warning_id;
    }
    else if(mode == MM_ID_MODE)
    {
        for(i=0; i<num; i++)
        {
            s_mm_id ++;
            id[i] = s_mm_id;
        }
    }
    else
    {
        warn_id = 0;
    }
    pthread_mutex_unlock(&id_lock);

    return warn_id;
}

//流水号处理 
void do_serial_num(uint16_t *num, int mode)
{
    static uint16_t send_serial_num= 0;
    static uint16_t recv_serial_num= 0;
    static pthread_mutex_t serial_num_lock = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&serial_num_lock);
    if(mode == GET_NEXT_SEND_NUM)
    {
        if(send_serial_num == 0xFF)
            send_serial_num = 0;
        else
        {
            send_serial_num ++;
            *num = send_serial_num;
        }
    }
    //记录接收到的序列号，发送序列号在此基础上累加 
    else if(mode == RECORD_RECV_NUM)
    {
        recv_serial_num = *num;
        if(*num > send_serial_num)
        {
            send_serial_num = *num;
        }
    }
    else if(mode == GET_RECV_NUM)
    {
        *num = recv_serial_num; 
    }
    else
    {
    }
    pthread_mutex_unlock(&serial_num_lock);
}

pthread_mutex_t photo_queue_lock = PTHREAD_MUTEX_INITIALIZER;
queue<ptr_queue_node *> g_image_queue;
queue<ptr_queue_node *> *g_image_queue_p = &g_image_queue;

pthread_mutex_t req_cmd_queue_lock = PTHREAD_MUTEX_INITIALIZER;
queue<ptr_queue_node *> g_req_cmd_queue;
queue<ptr_queue_node *> *g_req_cmd_queue_p = &g_req_cmd_queue;

pthread_mutex_t ptr_queue_lock = PTHREAD_MUTEX_INITIALIZER;
queue<ptr_queue_node *> g_ptr_queue;
queue<ptr_queue_node *> *g_send_q_p = &g_ptr_queue;

pthread_mutex_t uchar_queue_lock = PTHREAD_MUTEX_INITIALIZER;
queue<uint8_t> g_uchar_queue;

pkg_repeat_status g_pkg_status;
pkg_repeat_status *g_pkg_status_p = &g_pkg_status;

void send_stat_pkg_init()
{
    memset(g_pkg_status_p, 0, sizeof(pkg_repeat_status));
}

//推入队列，可以只有node的header，数据可以为空
static int ptr_queue_push(queue<ptr_queue_node *> *p, ptr_queue_node *in,  pthread_mutex_t *lock)
{
    int ret;
    ptr_queue_node *header = NULL;
    uint8_t *ptr = NULL;

    if(!in || !p)
        return -1;

    pthread_mutex_lock(lock);
    if((int)p->size() == PTR_QUEUE_BUF_CNT)
    {
        printf("ptr queue overflow...\n");
        ret = -1;
        goto out;
    }
    else
    {
        header = (ptr_queue_node *)malloc(sizeof(ptr_queue_node));
        if(!header)
        {
            perror("ptr_queue_push malloc1");
            ret = -1;
            goto out;
        }

        memcpy(header, in, sizeof(ptr_queue_node));
        if(!in->buf)//user don't need buffer
        {
            header->len = 0;
            header->buf = NULL;
        }
        else //user need buffer
        {
            ptr = (uint8_t *)malloc(PTR_QUEUE_BUF_SIZE);
            if(!ptr)
            {
                perror("ptr_queue_push malloc2");
                free(header);
                ret = -1;
                goto out;
            }

            header->buf = ptr;
            header->len = in->len > PTR_QUEUE_BUF_SIZE ? PTR_QUEUE_BUF_SIZE : in->len;
            memcpy(header->buf, in->buf, header->len);
        }

        p->push(header);
        ret = 0;
        goto out;
    }

out:
    pthread_mutex_unlock(lock);
    return ret;
}

//弹出队列，可以只取node的header，数据不要
static int ptr_queue_pop(queue<ptr_queue_node*> *p, ptr_queue_node *out,  pthread_mutex_t *lock)
{
    ptr_queue_node *header = NULL;
    uint32_t user_buflen = 0;
    uint8_t *ptr = NULL;

    if(!out || !p)
        return -1;

    pthread_mutex_lock(lock);
    if(!p->empty())
    {
        header = p->front();
        p->pop();
    }
    pthread_mutex_unlock(lock);

    if(!header)
        return -1;

    //no data in node
    if(!header->buf)
    {
        //printf("no data in node!\n");
        memcpy(out, header, sizeof(ptr_queue_node));
    }
    //node have data,
    else
    {
        //user don't need data
        if(!out->buf)
        {
            //printf("user no need data out!\n");
            memcpy(out, header, sizeof(ptr_queue_node));
        }
        else
        {
            //record ptr and len
            ptr = out->buf;
            user_buflen = out->len;
            memcpy(out, header, sizeof(ptr_queue_node));
            out->buf = ptr;
            //get the min len
            header->len = header->len > PTR_QUEUE_BUF_SIZE ? PTR_QUEUE_BUF_SIZE : header->len;
            out->len = header->len > user_buflen ? user_buflen : header->len;
            memcpy(out->buf, header->buf, out->len);
        }
        free(header->buf);
    }

    free(header);
    return 0;
}


//return 0, 超时，单位是毫秒 
int timeout_trigger(struct timeval *tv, int ms)
{
    struct timeval tv_cur;
    int timeout_sec, timeout_usec;

    timeout_sec = (ms)/1000;
    timeout_usec  = ((ms)%1000)*1000;

    gettimeofday(&tv_cur, NULL);

    if((tv_cur.tv_sec > tv->tv_sec + timeout_sec) || \
            ((tv_cur.tv_sec == tv->tv_sec + timeout_sec) && (tv_cur.tv_usec > tv->tv_usec + timeout_usec)))
    {
        printf("timeout_trigger! %d ms\n", ms);
        return 1;
    }
    else
        return 0;
}

//return if error happened, or data write over
static void package_write(int sock, uint8_t *buf, int len)
{
    int ret = 0;
    int offset = 0;

    if(sock < 0 || len < 0 || !buf){
        return ;
    } else{
        while(offset < len)
        {
            ret = write(sock, &buf[offset], len-offset);
            if(ret <= 0){
                //write error deal
                perror("tcp write:");
                if(errno == EINTR || errno == EAGAIN){
                    printf("package write wait mommemt, continue!\n");
                    usleep(10000);
                    continue;
                }else
                    return ;
                    //return -1;
            }else{
                offset += ret;
            }
        }
    }
    //return offset;
    return ;
}

//insert mm info 
void push_mm_queue(InfoForStore *mm)
{
    ptr_queue_node header;
    header.buf = NULL;
    header.len = 0;

#if 0
    pthread_mutex_lock(&save_mp4_mutex);
    save_mp4 = 1;
    pthread_cond_signal(&save_mp4_cond);
    pthread_mutex_unlock(&save_mp4_mutex);
#endif

    memcpy(&header.mm, mm, sizeof(*mm));

    ptr_queue_push(g_image_queue_p, &header, &photo_queue_lock);
}

//pull node ,the info use to record the mp4 or jpeg
int pull_mm_queue(InfoForStore *mm)
{
    ptr_queue_node header;
    header.buf = NULL;
    header.len = 0;


    if(!ptr_queue_pop(g_image_queue_p, &header, &photo_queue_lock))
    {
        memcpy(mm, &header.mm, sizeof(*mm));
        return 0;
    }

    return -1;
}

//store req mm cmd
void push_mm_req_cmd_queue(send_mm_info *mm_info)
{
    ptr_queue_node header;
    header.buf = NULL;
    header.len = 0;

    memcpy(&header.mm_info, mm_info, sizeof(*mm_info));

    //printf("push id = 0x%08x, type=%x\n", mm_info->id, mm_info->type);
    ptr_queue_push(g_req_cmd_queue_p, &header, &req_cmd_queue_lock);
}
//pull req cmd
int pull_mm_req_cmd_queue(send_mm_info *mm_info)
{
    ptr_queue_node header;
    header.buf = NULL;
    header.len = 0;
    if(!ptr_queue_pop(g_req_cmd_queue_p, &header, &req_cmd_queue_lock))
    {
        memcpy(mm_info, &header.mm_info, sizeof(*mm_info));
        //printf("id = 0x%08x, type=%x\n", header.mm_info.id, header.mm_info.type);
        return 0;
    }

    return -1;
}

//填写报警信息的一些实时数据
void get_real_time_msg(warningtext *uploadmsg)
{
    real_time_data tmp;
    RealTimeDdata_process(&tmp, READ_REAL_TIME_MSG);

    uploadmsg->altitude = tmp.altitude;
    uploadmsg->latitude = tmp.latitude;
    uploadmsg->longitude = tmp.longitude;
    uploadmsg->car_speed = tmp.car_speed;


    memcpy(uploadmsg->time, tmp.time, sizeof(uploadmsg->time));
    memcpy(&uploadmsg->car_status, &tmp.car_status, sizeof(uploadmsg->car_status));
}


void get_adas_Info_for_store(uint8_t type, InfoForStore *mm_store)
{
    adas_para_setting para;

    read_dev_para(&para, SAMPLE_DEVICE_ID_ADAS);
    mm_store->warn_type = type;
    switch(type)
    {
        case SW_TYPE_FCW:
        case SW_TYPE_LDW:
        case SW_TYPE_HW:
        case SW_TYPE_PCW:
        case SW_TYPE_FLC:
            if(type == SW_TYPE_FCW){
                mm_store->photo_num = para.FCW_photo_num;
                mm_store->photo_time_period = para.FCW_photo_time_period;
                mm_store->video_time = para.FCW_video_time;
            }else if(type == SW_TYPE_LDW){
                mm_store->photo_num = para.LDW_photo_num;
                mm_store->photo_time_period = para.LDW_photo_time_period;
                mm_store->video_time = para.LDW_video_time;
            }else if(type == SW_TYPE_HW){
                mm_store->photo_num = para.HW_photo_num;
                mm_store->photo_time_period = para.HW_photo_time_period;
                mm_store->video_time = para.HW_video_time;
            }else if(type == SW_TYPE_PCW){
                mm_store->photo_num = para.PCW_photo_num;
                mm_store->photo_time_period = para.PCW_photo_time_period;
                mm_store->video_time = para.PCW_video_time;
            }else if(type == SW_TYPE_FLC){
                mm_store->photo_num = para.FLC_photo_num;
                mm_store->photo_time_period = para.FLC_photo_time_period;
                mm_store->video_time = para.FLC_video_time;
            }else{
                ;
            }
            if(mm_store->video_time != 0)
                mm_store->video_enable = 1; 
            else
                mm_store->video_enable = 0; 

            if((mm_store->photo_num != 0 && mm_store->photo_num < WARN_SNAP_NUM_MAX))
                mm_store->photo_enable = 1; 
            else
                mm_store->photo_enable = 0; 
        case SW_TYPE_TSRW:
        case SW_TYPE_TSR:
            break;

        case SW_TYPE_SNAP:
            mm_store->photo_num = para.photo_num;
            mm_store->photo_time_period = para.photo_time_period;
            mm_store->photo_enable = 1; 

        default:
            break;
    }
}

void get_dms_Info_for_store(uint8_t type, InfoForStore *mm_store)
{
    dms_para_setting para;

    read_dev_para(&para, SAMPLE_DEVICE_ID_DMS);
    mm_store->warn_type = type;
    switch(type)
    {
        case DMS_FATIGUE_WARN:
        case DMS_CALLING_WARN:
        case DMS_SMOKING_WARN:
        case DMS_DISTRACT_WARN:
        case DMS_ABNORMAL_WARN:

            if(type == DMS_FATIGUE_WARN){
                mm_store->photo_num = para.FatigueDriv_PhotoNum;
                mm_store->photo_time_period = para.FatigueDriv_PhotoInterval;
                mm_store->video_time = para.FatigueDriv_VideoTime;
            }else if(type == DMS_CALLING_WARN){
                mm_store->photo_num = para.CallingDriv_PhotoNum;
                mm_store->photo_time_period = para.CallingDriv_PhotoInterval;
                mm_store->video_time = para.CallingDriv_VideoTime;
            }else if(type == DMS_SMOKING_WARN){
                mm_store->photo_num = para.SmokingDriv_PhotoNum;
                mm_store->photo_time_period = para.SmokingDriv_PhotoInterval;
                mm_store->video_time = para.SmokingDriv_VideoTime;
            }else if(type == DMS_DISTRACT_WARN){
                mm_store->photo_num = para.DistractionDriv_PhotoNum;
                mm_store->photo_time_period = para.DistractionDriv_PhotoInterval;
                mm_store->video_time = para.DistractionDriv_VideoTime;
            }else if(type == DMS_ABNORMAL_WARN){
                mm_store->photo_num = para.AbnormalDriv_PhotoNum;
                mm_store->photo_time_period = para.AbnormalDriv_PhotoInterval;
                mm_store->video_time = para.AbnormalDriv_VideoTime;
            }else{
                ;
            }
            if(mm_store->video_time != 0)
                mm_store->video_enable = 1; 
            else
                mm_store->video_enable = 0; 

            if((mm_store->photo_num != 0 && mm_store->photo_num < WARN_SNAP_NUM_MAX))
                mm_store->photo_enable = 1; 
            else
                mm_store->photo_enable = 0; 


        case DMS_DRIVER_CHANGE:
            break;

        case DMS_SANPSHOT_EVENT:
            mm_store->photo_num = para.photo_num;
            mm_store->photo_time_period = para.photo_time_period;
            mm_store->photo_enable = 1; 

        default:
            break;
    }
}


int filter_alert_by_time(unsigned int *last, unsigned int secs)
{
    struct timeval tv; 

#ifndef FILTER_ALERT_BY_SPEED
    return 1;
#endif

    gettimeofday(&tv, NULL);

    if (tv.tv_sec - (*last) < secs)
        return 0;

    *last = tv.tv_sec;

    return 1;
}



int filter_alert_by_speed()
{
    real_time_data tmp;
    adas_para_setting para;

#ifndef FILTER_ALERT_BY_SPEED
    return 1;
#endif

    read_dev_para(&para, SAMPLE_DEVICE_ID_ADAS);
    RealTimeDdata_process(&tmp, READ_REAL_TIME_MSG);

    if(tmp.car_speed <= para.warning_speed_val)
    {
        printf("filter low speed (%d) alert!\n", tmp.car_speed);
        return 0;
    }

    return 1;
}




/*********************************
* func: build adas warning package
* return: framelen
*********************************/
int build_adas_warn_frame(int type, warningtext *uploadmsg)
{
    int i=0;
    InfoForStore mm;
    adas_para_setting para;

    read_dev_para(&para, SAMPLE_DEVICE_ID_ADAS);
    memset(&mm, 0, sizeof(mm));
    get_adas_Info_for_store(type, &mm);

    get_local_time(uploadmsg->time);

    uploadmsg->warning_id = MY_HTONL(get_next_id(WARNING_ID_MODE, NULL, 0));
    uploadmsg->sound_type = type;
    uploadmsg->mm_num = 0;
    get_real_time_msg(uploadmsg);
    

    switch(type)
    {
        case SW_TYPE_FCW:
        case SW_TYPE_LDW:
        case SW_TYPE_HW:
        case SW_TYPE_PCW:
        case SW_TYPE_FLC:
            if(mm.photo_enable)
            {
                get_next_id(MM_ID_MODE, mm.photo_id, mm.photo_num);
                for(i=0; i<mm.photo_num; i++)
                {
                    //printf("mm.photo_id[%d] = %d\n", i, mm.photo_id[i]);
                    uploadmsg->mm_num++;
                    uploadmsg->mm[i].type = MM_PHOTO;
                    uploadmsg->mm[i].id = MY_HTONL(mm.photo_id[i]);
                }
            }
            if(mm.video_enable)
            {
                get_next_id(MM_ID_MODE, mm.video_id, 1);
                uploadmsg->mm_num++;
                uploadmsg->mm[i].type = MM_VIDEO;
                uploadmsg->mm[i].id = MY_HTONL(mm.video_id[0]);
                i++;
            }
            mm.flag = 0;
            push_mm_queue(&mm);

#if 1
            //add dms video

            if(mm.video_enable){
                mm.video_enable = 1; 
                mm.photo_enable = 0; 
                if(mm.video_enable)
                {
                    get_next_id(MM_ID_MODE, mm.video_id, 1);
                    uploadmsg->mm_num++;
                    uploadmsg->mm[i].type = MM_VIDEO;
                    uploadmsg->mm[i].id = MY_HTONL(mm.video_id[0]);
                }
                mm.flag = 1;
                push_mm_queue(&mm);
            }
#endif

            break;

        case SW_TYPE_TSRW:
        case SW_TYPE_TSR:
            break;

        case SW_TYPE_SNAP:
            //printf("snap: enable\n");
            if(mm.photo_enable)
            {
                mm.warn_type = type;
                get_next_id(MM_ID_MODE, mm.photo_id, mm.photo_num);
                for(i=0; i<mm.photo_num; i++)
                {
                    uploadmsg->mm_num++;
                    uploadmsg->mm[i].type = MM_PHOTO;
                    uploadmsg->mm[i].id = MY_HTONL(mm.photo_id[i]);
                }
                mm.flag = 0;
                push_mm_queue(&mm);
            }
            break;

        default:
            break;
    }
    return (sizeof(*uploadmsg) + uploadmsg->mm_num*sizeof(sample_mm_info));
}




/*********************************
* func: build dms warning package
* return: framelen
*********************************/
int build_dms_warn_frame(int type, dms_warningtext *uploadmsg)
{
    int i=0;
    InfoForStore mm;
    dms_para_setting para;
    real_time_data tmp;

    read_dev_para(&para, SAMPLE_DEVICE_ID_DMS);
    memset(&mm, 0, sizeof(mm));
    get_dms_Info_for_store(type, &mm);

    uploadmsg->warning_id = MY_HTONL(get_next_id(WARNING_ID_MODE, NULL, 0));
    uploadmsg->sound_type = type;
    uploadmsg->mm_num = 0;


    get_local_time(uploadmsg->time);

    RealTimeDdata_process(&tmp, READ_REAL_TIME_MSG);
    uploadmsg->altitude = tmp.altitude;
    uploadmsg->latitude = tmp.latitude;
    uploadmsg->longitude = tmp.longitude;
    uploadmsg->car_speed = tmp.car_speed;
    memcpy(uploadmsg->time, tmp.time, sizeof(uploadmsg->time));
    memcpy(&uploadmsg->car_status, &tmp.car_status, sizeof(uploadmsg->car_status));

    switch(type)
    {
        case DMS_FATIGUE_WARN:
        case DMS_CALLING_WARN:
        case DMS_SMOKING_WARN:
        case DMS_DISTRACT_WARN:
        case DMS_ABNORMAL_WARN:
            
            if(mm.photo_enable)
            {
                WSI_DEBUG("dms photo_enbale! num = %d\n", mm.photo_num);
                get_next_id(MM_ID_MODE, mm.photo_id, mm.photo_num);
                for(i=0; i<mm.photo_num; i++)
                {
                    //printf("mm.photo_id[%d] = %d\n", i, mm.photo_id[i]);
                    uploadmsg->mm_num++;
                    uploadmsg->mm[i].type = MM_PHOTO;
                    uploadmsg->mm[i].id = MY_HTONL(mm.photo_id[i]);
                }
            }
            if(mm.video_enable)
            {
                WSI_DEBUG("dms video_enbale!\n");
                get_next_id(MM_ID_MODE, mm.video_id, 1);
                uploadmsg->mm_num++;
                uploadmsg->mm[i].type = MM_VIDEO;
                uploadmsg->mm[i].id = MY_HTONL(mm.video_id[0]);
                i++;
            }
            mm.flag = 0;
            push_mm_queue(&mm);
            
            WSI_DEBUG("num2 = %d\n", uploadmsg->mm_num);

            if(mm.video_enable)
            {
                //add  video
                mm.video_enable = 1; 
                mm.photo_enable = 0; 
                if(mm.video_enable)
                {
                    get_next_id(MM_ID_MODE, mm.video_id, 1);
                    uploadmsg->mm_num++;
                    uploadmsg->mm[i].type = MM_VIDEO;
                    uploadmsg->mm[i].id = MY_HTONL(mm.video_id[0]);
                }
                mm.flag = 1;
                push_mm_queue(&mm);
                WSI_DEBUG("num3 = %d\n", uploadmsg->mm_num);
            }

            break;

        case DMS_DRIVER_CHANGE:
            break;

        case DMS_SANPSHOT_EVENT:
            if(mm.photo_enable)
            {
                mm.warn_type = type;
                get_next_id(MM_ID_MODE, mm.photo_id, mm.photo_num);
                for(i=0; i<mm.photo_num; i++)
                {
                    uploadmsg->mm_num++;
                    uploadmsg->mm[i].type = MM_PHOTO;
                    uploadmsg->mm[i].id = MY_HTONL(mm.photo_id[i]);
                }
                mm.flag = 0;
                push_mm_queue(&mm);
            }
            break;

        default:
            break;
    }
    return (sizeof(*uploadmsg) + uploadmsg->mm_num*sizeof(sample_mm_info));
}

#if 0
void recv_dms_message(can_data_type *can)
{
#if 1
    dms_alert_info msg;
    uint32_t playloadlen = 0;
    uint8_t msgbuf[512];
    uint8_t txbuf[512];
    uint8_t i=0;
    int alert_type = 0;
    static unsigned int dms_fatigue_warn = 0;
    static unsigned int dms_distract_warn = 0;
    static unsigned int dms_calling_warn = 0;
    static unsigned int dms_smoking_warn = 0;
    static unsigned int dms_abnormal_warn = 0;

    static uint8_t dms_alert_last[8] = {0,0,0,0,0,0,0,0};
    uint8_t dms_alert[8] = {0,0,0,0,0,0,0,0};
    uint8_t dms_alert_mask[8] = {0xFF,0xFF,0,0,0,0,0,0};

    sample_prot_header *pSend = (sample_prot_header *) txbuf;
    dms_warningtext *uploadmsg = (dms_warningtext *)&msgbuf[0];

#if 0
    printf("soure: %s\n", can->source);
    printf("time: %ld\n", can->time);
    printf("topic: %s\n", can->topic);
    printbuf(can->warning, sizeof(can->warning));
#endif

    memcpy(&msg, can->warning, sizeof(can->warning));
    if(!filter_alert_by_speed())
        goto out;


    for(i=0; i<sizeof(can->warning); i++)
    {
        dms_alert[i] = can->warning[i] & dms_alert_mask[i];
    }

    //filter the same alert
    if(!memcmp(dms_alert, dms_alert_last, sizeof(dms_alert)))
    {
        printf("alert is the same!\n");
        goto out;
    }

#if 0
    printf("msg.alert_eye_close1 %d\n", msg.alert_eye_close1);
    printf("msg.alert_eye_close2 %d\n", msg.alert_eye_close2);
    printf("msg.alert_look_around %d\n", msg.alert_look_around);
    printf("msg.alert_yawn %d\n", msg.alert_yawn);
    printf("msg.alert_phone %d\n", msg.alert_phone);
    printf("msg.alert_smoking %d\n", msg.alert_smoking);
    printf("msg.alert_absence %d\n", msg.alert_absence);
    printf("msg.alert_bow %d\n", msg.alert_bow);
#endif

    if(msg.alert_eye_close1 || msg.alert_eye_close2 || msg.alert_yawn || msg.alert_bow){
        alert_type = DMS_FATIGUE_WARN;
        if(!filter_alert_by_time(&dms_fatigue_warn, FILTER_DMS_ALERT_SET_TIME))
        {
      //      printf("dms filter alert by time!");
            goto out;
        }
    }else if (msg.alert_look_around ){
        alert_type = DMS_DISTRACT_WARN;
        if(!filter_alert_by_time(&dms_distract_warn, FILTER_DMS_ALERT_SET_TIME))
        {
        //    printf("dms filter alert by time!");
            goto out;
        }
    }else if(msg.alert_phone){
        alert_type = DMS_CALLING_WARN;
        if(!filter_alert_by_time(&dms_calling_warn, FILTER_DMS_ALERT_SET_TIME))
        {
          //  printf("dms filter alert by time!");
            goto out;
        }
    }else if(msg.alert_smoking){
        alert_type = DMS_SMOKING_WARN;
        if(!filter_alert_by_time(&dms_smoking_warn, FILTER_DMS_ALERT_SET_TIME))
        {
            //printf("dms filter alert by time!");
            goto out;
        }
    }else if(msg.alert_absence){
        alert_type = DMS_ABNORMAL_WARN;
        if(!filter_alert_by_time(&dms_abnormal_warn, FILTER_DMS_ALERT_SET_TIME))
        {
            //printf("dms filter alert by time!");
            goto out;
        }
    }else
        goto out;

#if 1
    playloadlen = build_dms_warn_frame(alert_type, uploadmsg);
    printf("send dms alert %d!\n", alert_type);
    printf("dms alert frame len = %ld\n", sizeof(*uploadmsg));
    printbuf((uint8_t *)uploadmsg, playloadlen);
    sample_assemble_msg_to_push(pSend, \
            SAMPLE_DEVICE_ID_DMS,\
            SAMPLE_CMD_WARNING_REPORT,\
            (uint8_t *)uploadmsg,\
            playloadlen);
#endif

out:
    memcpy(dms_alert_last, dms_alert, sizeof(dms_alert));
    return;
#endif
}
#else
void recv_dms_message(dms_can_779 *msg)
{
    uint32_t playloadlen = 0;
    uint8_t msgbuf[512];
    uint8_t txbuf[512];
    uint8_t i=0;
    int alert_type = 0;
    static unsigned int dms_fatigue_warn = 0;
    static unsigned int dms_distract_warn = 0;
    static unsigned int dms_calling_warn = 0;
    static unsigned int dms_smoking_warn = 0;
    static unsigned int dms_abnormal_warn = 0;

    static uint8_t dms_alert_last[8] = {0,0,0,0,0,0,0,0};
    uint8_t dms_alert[8] = {0,0,0,0,0,0,0,0};
    uint8_t dms_alert_mask[8] = {0xFF,0xFF,0,0,0,0,0,0};

    sample_prot_header *pSend = (sample_prot_header *) txbuf;
    dms_warningtext *uploadmsg = (dms_warningtext *)&msgbuf[0];

    if(!filter_alert_by_speed())
        goto out;

    memcpy(&dms_alert, msg, sizeof(dms_can_779));
    for(i=0; i<sizeof(dms_can_779); i++){
        dms_alert[i] &= dms_alert_mask[i];
    }

    //filter the same alert
    if(!memcmp(dms_alert, dms_alert_last, sizeof(dms_alert))){
        printf("filter the same alert!\n");
        goto out;
    }

    if(msg->Eye_Closure_Warning || msg->Yawn_warning){
        alert_type = DMS_FATIGUE_WARN;
        if(!filter_alert_by_time(&dms_fatigue_warn, FILTER_DMS_ALERT_SET_TIME)){
      //      printf("dms filter alert by time!");
            goto out;
        }
    }else if (msg->Look_up_warning || msg->Look_around_warning || msg->Look_down_warning){
        alert_type = DMS_DISTRACT_WARN;
        if(!filter_alert_by_time(&dms_distract_warn, FILTER_DMS_ALERT_SET_TIME)){
        //    printf("dms filter alert by time!");
            goto out;
        }
    }else if(msg->Phone_call_warning){
        alert_type = DMS_CALLING_WARN;
        if(!filter_alert_by_time(&dms_calling_warn, FILTER_DMS_ALERT_SET_TIME)){
          //  printf("dms filter alert by time!");
            goto out;
        }
    }else if(msg->Smoking_warning){
        alert_type = DMS_SMOKING_WARN;
        if(!filter_alert_by_time(&dms_smoking_warn, FILTER_DMS_ALERT_SET_TIME)){
            //printf("dms filter alert by time!");
            goto out;
        }
    }else if(msg->Absence_warning){
        alert_type = DMS_ABNORMAL_WARN;
        if(!filter_alert_by_time(&dms_abnormal_warn, FILTER_DMS_ALERT_SET_TIME)){
            //printf("dms filter alert by time!");
            goto out;
        }
    }else
        goto out;

    playloadlen = build_dms_warn_frame(alert_type, uploadmsg);
    WSI_DEBUG("send dms alert %d!\n", alert_type);
    WSI_DEBUG("dms alert frame len = %ld\n", sizeof(*uploadmsg));
    printbuf((uint8_t *)uploadmsg, playloadlen);
    sample_assemble_msg_to_push(pSend, \
            SAMPLE_DEVICE_ID_DMS,\
            SAMPLE_CMD_WARNING_REPORT,\
            (uint8_t *)uploadmsg,\
            playloadlen);

out:
    memcpy(dms_alert_last, dms_alert, sizeof(dms_alert));
    return;
}

#endif

static int send_package(int sock, uint8_t *buf)
{
    int ret = 0;
    int len = 0;
    SendStatus pkg;
    struct timespec outtime;

    if(sock < 0)
    {
        printf("sock error\n");
        return -1;
    }

    ptr_queue_node header;
    header.buf = buf;
    header.len = PTR_QUEUE_BUF_SIZE;

    //fail
    if(ptr_queue_pop(g_send_q_p, &header, &ptr_queue_lock)){
        
        printf("queue no mesg\n");
        goto out;
    }
    memcpy(&pkg, &header.pkg, sizeof(header.pkg));

    WSI_DEBUG("header len = %d\n", header.len);
    //printbuf(header.buf, header.len);
    if(pkg.ack_status == MSG_ACK_READY){// no need ack
        printf("no need ack!\n");
        package_write(sock, header.buf, header.len);
        goto out;
    }
    if(pkg.ack_status == MSG_ACK_WAITING){
        printf("ack waiting!\n");
        package_write(sock, header.buf, header.len);
        pkg.send_repeat++;

        //waiting ack
        pthread_mutex_lock(&recv_ack_mutex);
        while(1)
        {
            clock_gettime(CLOCK_REALTIME, &outtime);
            outtime.tv_sec += 2;
            pthread_cond_timedwait(&recv_ack_cond, &recv_ack_mutex, &outtime);
            if(recv_ack == NOTICE_MSG){
                recv_ack= WAIT_MSG;//clear
                printf("recv send ack..\n");
                break;
            }else{
                printf("recv ack timeout0!\n");
                if(pkg.send_repeat >= 3){//第一次发送
                    printf("send three times..\n");
                    g_pkg_status_p->mm_data_trans_waiting = 0;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&recv_ack_mutex);
    }

out:
    return 0;
}

void *pthread_tcp_send(void *para)
{
    uint8_t *writebuf = NULL;
    writebuf = (uint8_t *)malloc(PTR_QUEUE_BUF_SIZE);
    if(!writebuf){
        perror("send pkg malloc");
        goto out;
    }

    while(1)
    {
        printf("sem waiting...\n");
        sem_wait(&send_data);
        //send_pkg_to_host(hostsock, writebuf);
        send_package(hostsock, writebuf);
    }
out:
    if(writebuf)
        free(writebuf);

    pthread_exit(NULL);
}

#if 0
static int uchar_queue_push(uint8_t *ch)
{
    int ret = -1;
    if(!ch)
        return ret;

    pthread_mutex_lock(&uchar_queue_lock);
    if((int)g_uchar_queue.size() == UCHAR_QUEUE_SIZE)
    {
        printf("g_uchar_queue full flow\n");
        ret = -1;
        goto out;
    }
    else
    {
        g_uchar_queue.push(ch[0]);
        ret = 0;
        goto out;
    }
out:
    pthread_mutex_unlock(&uchar_queue_lock);
    return ret;
}
static int8_t uchar_queue_pop(uint8_t *ch)
{
    int ret = -1;
    if(!ch)
        return ret;

    pthread_mutex_lock(&uchar_queue_lock);
    if(!g_uchar_queue.empty())
    {
        *ch = g_uchar_queue.front();
        g_uchar_queue.pop();
        ret = 0;
        goto out;
    }
    else
    {
        ret = -1;
        goto out;
    }
out:
    pthread_mutex_unlock(&uchar_queue_lock);
    return ret;
}
#endif

static uint8_t sample_calc_sum(sample_prot_header *pHeader, int32_t msg_len)
{
    int32_t i = 0;
    uint32_t chksum = 0;
    uint8_t * start = (uint8_t *) &pHeader->vendor_id;

#define NON_CRC_LEN (2 * sizeof(pHeader->magic) /*head and tail*/ + \
        sizeof(pHeader->serial_num) + \
        sizeof(pHeader->checksum))

    for (i = 0; i < int32_t(msg_len - NON_CRC_LEN); i++)
    {
        chksum += start[i];

        //   MY_DEBUG("#%04d 0x%02hhx = 0x%08x\n", i, start[i], chksum);
    }
    return (uint8_t) (chksum & 0xFF);
}

static int32_t sample_escaple_msg(sample_prot_header *pHeader, int32_t msg_len)
{
    int32_t i = 0;
    int32_t escaped_len = msg_len;
    uint8_t *barray = (uint8_t*) pHeader;

    //ignore head/tail magic
    for (i = 1; i < escaped_len - 1; i++)
    {
        if (SAMPLE_PROT_MAGIC == barray[i]) 
        {
            memmove(&barray[i+1], &barray[i], escaped_len - i);
            barray[i] = SAMPLE_PROT_ESC_CHAR;
            barray[i+1] = 0x2;
            i++;
            escaped_len ++;
        }
        else if (SAMPLE_PROT_ESC_CHAR == barray[i]) 
        {
            memmove(&barray[i+1], &barray[i], escaped_len - i);
            barray[i]   = SAMPLE_PROT_ESC_CHAR;
            barray[i+1] = 0x1;
            i++;
            escaped_len ++;
        }
    }
    return escaped_len;
}

//push到发送队列
int32_t sample_assemble_msg_to_push(sample_prot_header *pHeader, uint8_t devid, uint8_t cmd,uint8_t *payload, int32_t payload_len)
{
    uint16_t serial_num = 0;
    char MasterIsMe = 0;
    ptr_queue_node msg;
    int32_t msg_len = sizeof(*pHeader) + 1 + payload_len;
    uint8_t *data = ((uint8_t*) pHeader + sizeof(*pHeader));
    uint8_t *tail = data + payload_len;

    switch(cmd)
    {
        case SAMPLE_CMD_QUERY:
        case SAMPLE_CMD_FACTORY_RESET:
        case SAMPLE_CMD_SPEED_INFO:        
        case SAMPLE_CMD_DEVICE_INFO:
        case SAMPLE_CMD_UPGRADE:
        case SAMPLE_CMD_GET_PARAM:
        case SAMPLE_CMD_SET_PARAM:
        case SAMPLE_CMD_REQ_STATUS:
        case SAMPLE_CMD_REQ_MM_DATA:
            msg.pkg.ack_status = MSG_ACK_READY;
            MasterIsMe = 0;
            break;

            //send as master
        case SAMPLE_CMD_WARNING_REPORT:
        case SAMPLE_CMD_UPLOAD_MM_DATA:
        case SAMPLE_CMD_UPLOAD_STATUS:
            MasterIsMe = 1;
            msg.pkg.ack_status = MSG_ACK_WAITING;
            break;

        default:
            msg.pkg.ack_status = MSG_ACK_READY;
            break;
    }

    memset(pHeader, 0, sizeof(*pHeader));
    pHeader->magic = SAMPLE_PROT_MAGIC;

    //如果当前发送的数据是主动发送，即需要ACK的，序列号就直接累加
    if(MasterIsMe)
        do_serial_num(&serial_num, GET_NEXT_SEND_NUM);
    else
        do_serial_num(&serial_num, GET_RECV_NUM);

    pHeader->serial_num= MY_HTONS(serial_num); //used as message cnt
    pHeader->vendor_id= MY_HTONS(VENDOR_ID);
    //pHeader->device_id= SAMPLE_DEVICE_ID_ADAS;

#if defined ENABLE_ADAS
    pHeader->device_id= SAMPLE_DEVICE_ID_ADAS;
#elif defined ENABLE_DMS
    pHeader->device_id= SAMPLE_DEVICE_ID_DMS;
#endif
    pHeader->cmd = cmd;

    if (payload_len > 0) 
    {
        memcpy(data, payload, payload_len);
    }
    tail[0] = SAMPLE_PROT_MAGIC;

    pHeader->checksum = sample_calc_sum(pHeader, msg_len);
    msg_len = sample_escaple_msg(pHeader, msg_len);

    msg.pkg.send_repeat = 0;
    msg.len = msg_len;
    msg.buf = (uint8_t *)pHeader;
    //    printf("sendpackage cmd = 0x%x,msg.need_ack = %d, len=%d, push!\n",msg.cmd, msg.need_ack, msg.len);
    ptr_queue_push(g_send_q_p, &msg, &ptr_queue_lock);

    printf("posting...\n");
    sem_post(&send_data);
    //printf("push queue, len = %d\n", msg.len);

    return msg_len;
}

void send_snap_shot_ack(sample_prot_header *pHeader, int32_t len)
{
    uint8_t txbuf[256] = {0};
    uint8_t ack = 0;
    sample_prot_header *pSend = (sample_prot_header *) txbuf;

    if(len == sizeof(sample_prot_header) + 1)
    {
        sample_assemble_msg_to_push(pSend, pHeader->device_id, SAMPLE_CMD_SNAP_SHOT, (uint8_t *)&ack, 1);
    }
    else
    {
        printf("recv cmd:0x%x, data len maybe error!\n", pHeader->cmd);
    }
}

int do_snap_shot()
{
    uint32_t playloadlen = 0;
    uint8_t msgbuf[512];
    uint8_t txbuf[512];
    sample_prot_header *pSend = (sample_prot_header *) txbuf;

#if defined ENABLE_DMS
    dms_warningtext *uploadmsg = (dms_warningtext *)&msgbuf[0];
    playloadlen = build_dms_warn_frame(DMS_SANPSHOT_EVENT, uploadmsg);
    sample_assemble_msg_to_push(pSend, \
            SAMPLE_DEVICE_ID_DMS,\
            SAMPLE_CMD_WARNING_REPORT,\
            (uint8_t *)uploadmsg,\
            playloadlen);

#elif defined ENABLE_ADAS
    warningtext *uploadmsg = (warningtext *)&msgbuf[0];
    playloadlen = build_adas_warn_frame(SW_TYPE_SNAP, uploadmsg);
    printf("sanp len = %d\n", playloadlen);
    sample_assemble_msg_to_push(pSend, \
            SAMPLE_DEVICE_ID_ADAS,\
            SAMPLE_CMD_WARNING_REPORT,\
            (uint8_t *)uploadmsg,\
            playloadlen);
#endif

    return 0;
}

void set_BCD_time(warningtext *uploadmsg, uint64_t usec)
{
    struct tm *p = NULL; 
    time_t timep = 0;   
    printf("time:%ld\n", usec);
    //timep = strtoul(second, NULL, 10);
    //timep = timep/1000000;
    timep = usec/1000000;
    p = localtime(&timep);
    uploadmsg->time[0] = (p->tm_year+1900)%100;
    uploadmsg->time[1] = p->tm_mon+1;
    uploadmsg->time[2] = p->tm_mday;
    uploadmsg->time[3] = p->tm_hour;
    uploadmsg->time[4] = p->tm_min;
    uploadmsg->time[5] = p->tm_sec;

    printf("%d-%d-%d %d:%d:%d\n", (1900 + p->tm_year), ( 1 + p->tm_mon), p->tm_mday,(p->tm_hour), p->tm_min, p->tm_sec); 
}

static struct timeval test_time;  

static MECANWarningMessage g_last_warning_data;
static MECANWarningMessage g_last_can_msg;
static uint8_t g_last_trigger_warning[sizeof(MECANWarningMessage)];
int can_message_send(can_data_type *sourcecan)
{
    uint32_t warning_id = 0;
    uint8_t msgbuf[512];
    uint32_t playloadlen = 0;
    warningtext *uploadmsg = (warningtext *)&msgbuf[0];
    MECANWarningMessage can;
    car_info carinfo;
    uint8_t txbuf[512];
    sample_prot_header *pSend = (sample_prot_header *) txbuf;
    static unsigned int hw_alert = 0;
    static unsigned int fcw_alert = 0;
    static unsigned int ldw_alert = 0;



    uint32_t i = 0;
    uint8_t all_warning_masks[sizeof(MECANWarningMessage)] = {
        0x00, 0x00, 0x01, 0x00,
        0xFF, 0x00, 0x00, 0x03};
    uint8_t trigger_warning_masks[sizeof(MECANWarningMessage)] = {
        0x00, 0x00, 0x00, 0x00,
        0xFF, 0x00, 0x00, 0x00};
    uint8_t all_warning_data[sizeof(MECANWarningMessage)] = {0};
    uint8_t trigger_data[sizeof(MECANWarningMessage)] = {0};

    if(!memcmp(sourcecan->topic, MESSAGE_CAN700, strlen(MESSAGE_CAN700)))
    {
       // printf("700 come********************************\n");
        //printbuf(sourcecan->warning, 8);
#if 0

        if(!filter_alert_by_time(&ldw_alert, FILTER_ADAS_ALERT_SET_TIME))
        {
            printf("ldw filter alert by time!");
            return 0;
        }

        if(timeout_trigger(&test_time, 5*1000))//timeout
        {
            gettimeofday(&test_time, NULL);
            playloadlen = build_adas_warn_frame(SW_TYPE_FCW, uploadmsg);
            uploadmsg->start_flag = SW_STATUS_EVENT;
            uploadmsg->sound_type = SW_TYPE_FCW;
            sample_assemble_msg_to_push(pSend,SAMPLE_DEVICE_ID_ADAS, SAMPLE_CMD_WARNING_REPORT,\
                    (uint8_t *)uploadmsg, \
                    playloadlen);

            return 0;
        }
        
#endif
        memcpy(&can, sourcecan->warning, sizeof(sourcecan->warning));
        //printf("ldw_off = %d\n",can.ldw_off);
        //printf("left_ldw= %d\n",can.left_ldw);
        //printf("right_ldw= %d\n",can.right_ldw);
        //printf("fcw_on= %d\n",can.fcw_on);

        for (i = 0; i < sizeof(all_warning_masks); i++) {
            all_warning_data[i] = sourcecan->warning[i] & all_warning_masks[i];
            trigger_data[i]     = sourcecan->warning[i] & trigger_warning_masks[i];
        }

        if (0 == memcmp(all_warning_data, &g_last_warning_data, sizeof(g_last_warning_data))) {
            //printf("can exit!\n");
            return 0;
        }

        //filter alert
        if(!filter_alert_by_speed())
            goto out;
#if 1
        if( (g_last_warning_data.headway_warning_level != can.headway_warning_level) || \
                (0 != memcmp(trigger_data, &g_last_trigger_warning, sizeof(g_last_trigger_warning)) && 0 != trigger_data[4]) )
        {
            //printf("warning happened.........................\n");
            memset(msgbuf, 0, sizeof(msgbuf));
            //set_BCD_time(uploadmsg, sourcecan->time);
        }
#endif
        //LDW and FCW event
        if (0 != memcmp(trigger_data, &g_last_trigger_warning, sizeof(g_last_trigger_warning)) && 0 != trigger_data[4]) {
            //printf("------LDW/FCW event-----------\n");

            if (can.left_ldw || can.right_ldw) {


                if(!filter_alert_by_time(&ldw_alert, FILTER_ADAS_ALERT_SET_TIME))
                {
                    printf("ldw filter alert by time!");
                    goto out;
                }

                memset(uploadmsg, 0, sizeof(*uploadmsg));
                playloadlen = build_adas_warn_frame(SW_TYPE_LDW, uploadmsg);
                uploadmsg->start_flag = SW_STATUS_EVENT;

                if(can.left_ldw)
                    uploadmsg->ldw_type = SOUND_TYPE_LLDW;
                if(can.right_ldw)
                    uploadmsg->ldw_type = SOUND_TYPE_RLDW;

                WSI_DEBUG("send LDW alert message!\n");
                sample_assemble_msg_to_push(pSend,SAMPLE_DEVICE_ID_ADAS, SAMPLE_CMD_WARNING_REPORT,\
                        (uint8_t *)uploadmsg, \
                        playloadlen);
            }
            if (can.fcw_on) {

                if(!filter_alert_by_time(&fcw_alert, FILTER_ADAS_ALERT_SET_TIME))
                {
                    printf("ldw filter alert by time!");
                    goto out;
                }

                playloadlen = build_adas_warn_frame(SW_TYPE_FCW, uploadmsg);
                uploadmsg->start_flag = SW_STATUS_EVENT;
                uploadmsg->sound_type = SW_TYPE_FCW;

                WSI_DEBUG("send FCW alert message!\n");
                sample_assemble_msg_to_push(pSend,SAMPLE_DEVICE_ID_ADAS, SAMPLE_CMD_WARNING_REPORT,\
                        (uint8_t *)uploadmsg, \
                        playloadlen);
            }
        }
        //Headway
        if (g_last_warning_data.headway_warning_level != can.headway_warning_level) {
            //printf("------Headway event-----------\n");
            //printf("headway_warning_level:%d\n", can.headway_warning_level);
            //printf("headway_measurement:%d\n", can.headway_measurement);


            if(!filter_alert_by_time(&hw_alert, FILTER_ADAS_ALERT_SET_TIME))
            {
                printf("ldw filter alert by time!");
                goto out;
            }

            if (HW_LEVEL_RED_CAR == can.headway_warning_level) {
                playloadlen = build_adas_warn_frame(SW_TYPE_HW, uploadmsg);
                //uploadmsg->start_flag = SW_STATUS_BEGIN;
                uploadmsg->start_flag = SW_STATUS_EVENT;
                uploadmsg->sound_type = SW_TYPE_HW;

                WSI_DEBUG("send HW alert message!\n");
                sample_assemble_msg_to_push(pSend,SAMPLE_DEVICE_ID_ADAS, SAMPLE_CMD_WARNING_REPORT,\
                        (uint8_t *)uploadmsg, \
                        playloadlen);

            } else if (HW_LEVEL_RED_CAR == \
                    g_last_warning_data.headway_warning_level) {
                playloadlen = build_adas_warn_frame(SW_TYPE_HW, uploadmsg);
                //uploadmsg->start_flag = SW_STATUS_END;
                uploadmsg->start_flag = SW_STATUS_EVENT;
                uploadmsg->sound_type = SW_TYPE_HW;

                WSI_DEBUG("send HW alert2 message!\n");
                sample_assemble_msg_to_push(pSend,SAMPLE_DEVICE_ID_ADAS, SAMPLE_CMD_WARNING_REPORT,\
                        (uint8_t *)uploadmsg,\
                        playloadlen);
            }
        }


out:
        memcpy(&g_last_can_msg, &can, sizeof(g_last_can_msg));
        memcpy(&g_last_warning_data, all_warning_data, sizeof(g_last_warning_data));
        memcpy(&g_last_trigger_warning, trigger_data,\
                sizeof(g_last_trigger_warning));
    }
    if(!strncmp(sourcecan->topic, MESSAGE_CAN760, strlen(MESSAGE_CAN760)))
    {
        //printf("760 come.........................\n");
        //printbuf(sourcecan->warning, 8);
        memcpy(&carinfo, sourcecan->warning, sizeof(carinfo));
#if 0
        //			uploadmsg.high = carinfo.
        //			uploadmsg.altitude = carinfo. //MY_HTONS(altitude)
        //			uploadmsg.longitude= carinfo. //MY_HTONS(longitude)
        //			uploadmsg.car_status.acc = 
        uploadmsg->car_status.left_signal = carinfo.left_signal;
        uploadmsg->car_status.right_signal = carinfo.right_signal;

        if(carinfo.wipers_aval)
            uploadmsg->car_status.wipers = carinfo.wipers;
        uploadmsg->car_status.brakes = carinfo.brakes;
        //			uploadmsg.car_status.insert = 
#endif
    }
    //printf("can exit2!\n");
    return 0;
}

void get_local_time(uint8_t get_time[6])
{
    struct tm a;
    struct tm *p = &a;
    time_t timep = 0;   

    timep = time(NULL);

    p = localtime(&timep);
    get_time[0] = p->tm_year;
    get_time[1] = p->tm_mon+1;
    get_time[2] = p->tm_mday;
    get_time[3] = p->tm_hour;
    get_time[4] = p->tm_min;
    get_time[5] = p->tm_sec;

    printf("local time %d-%d-%d %d:%d:%d\n", (1900 + p->tm_year), ( 1 + p->tm_mon), p->tm_mday,(p->tm_hour), p->tm_min, p->tm_sec); 
}

void mmid_to_filename(uint32_t id, uint8_t type, char *filepath)
{
    if(type == MM_PHOTO)
        sprintf(filepath,"%s%08d.jpg", SNAP_SHOT_JPEG_PATH, id);
    else if(type == MM_AUDIO)
        sprintf(filepath,"%s%08d.wav", SNAP_SHOT_JPEG_PATH, id);
    else if(type == MM_VIDEO)
        sprintf(filepath,"%s%08d.mp4", SNAP_SHOT_JPEG_PATH, id);
    else
        ;
}

int find_local_image_name(uint8_t type, uint32_t id, char *filepath, uint32_t *filesize)
{
    mm_node node;

    //查找本地多媒体文件
    if(find_mm_resource(id, &node))
    {
        printf("find id[0x%x] fail!\n", id);
        return -1;
    }
    if(node.mm_type != type)
    {
        printf("find id[0x%x] fail, type error!\n", id);
        return -1;
    }


#if 0
    if(type == MM_PHOTO)
        sprintf(filepath,"%s%s-%08d.jpg", SNAP_SHOT_JPEG_PATH, warning_type_to_str(node.warn_type), id);
    else if(type == MM_AUDIO)
        sprintf(filepath,"%s%s-%08d.wav", SNAP_SHOT_JPEG_PATH, warning_type_to_str(node.warn_type), id);
    else if(type == MM_VIDEO)
        sprintf(filepath,"%s%s-%08d.mp4", SNAP_SHOT_JPEG_PATH, warning_type_to_str(node.warn_type), id);
    else
    {
        printf("mm type not valid, val=%d\n", type);
        return -1;
    }
#endif

    mmid_to_filename(id, type, filepath);

    if((*filesize = GetFileSize(filepath)) < 0)
        return -1;

    return 0;
}

int GetFileSize(char *filename)
{
    int filesize = 0;
    FILE *fp = NULL;

    printf("try open image filename: %s\n", filename);
    fp = fopen(filename, "rb");
    if(fp == NULL)
    {
        printf("open %s fail\n", filename);
        return -1;
    }
    else
    {
        printf("open %s ok\n", filename);
    }

    fseek(fp, 0, SEEK_SET);
    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    fclose(fp);

    return filesize;
}

//发送多媒体请求应答
static int32_t send_mm_req_ack(sample_prot_header *pHeader, int len)
{
    uint32_t mm_id = 0;
    uint8_t warn_type = 0;
    size_t filesize = 0;
    sample_mm_info *mm_ptr = NULL;
    send_mm_info send_mm;

    if(pHeader->cmd == SAMPLE_CMD_REQ_MM_DATA && !g_pkg_status_p->mm_data_trans_waiting) //recv req
    {
        g_pkg_status_p->mm_data_trans_waiting = 1;
        printf("------------req mm-------------\n");
        printbuf((uint8_t *)pHeader, len);

        //检查接收幀的完整性
        if(len == sizeof(sample_mm_info) + sizeof(sample_prot_header) + 1)
        {
            mm_ptr = (sample_mm_info *)(pHeader + 1);
            printf("req mm_type = 0x%x\n", mm_ptr->type);
            printf("req mm_id = 0x%08x\n", MY_HTONL(mm_ptr->id));
        }
        else
        {
            printf("recv cmd:0x%x, data len maybe error[%d]/[%ld]!\n", \
                    pHeader->cmd, len,\
                    sizeof(sample_mm_info) + sizeof(sample_prot_header) + 1);
            return -1;
        }
        //先应答请求，视频录制完成后在主动发送
        printf("send mm req ack!\n");
        send_mm.devid = pHeader->device_id;
        send_mm.id = mm_ptr->id;
        send_mm.type = mm_ptr->type;
        push_mm_req_cmd_queue(&send_mm);

        sample_assemble_msg_to_push(pHeader,pHeader->device_id, SAMPLE_CMD_REQ_MM_DATA, NULL, 0);
    }
    else
    {
        printf("current package is not valid!\n");
        return -1;
    }
    return 0;
}

static int recv_ack_and_send_image(sample_prot_header *pHeader, int32_t len)
{
    SendStatus pkg;
    sample_mm_ack mmack;

    WSI_DEBUG("recv ack...........!\n");
    memcpy(&mmack, pHeader+1, sizeof(mmack));
    if(mmack.ack){
        printf("recv ack err!\n");
        return -1;
    }else{
        WSI_DEBUG("index = 0x%08x, index2 = 0x%08x\n", g_pkg_status_p->mm.packet_index, mmack.packet_index);
        //recv ack index is correct
        if(MY_HTONS(g_pkg_status_p->mm.packet_index) == \
                (MY_HTONS(mmack.packet_index) + 1)){
            //改变发送包，接收ACK状态为ready
            notice_ack_msg();

            //最后一个ACK
            if(MY_HTONS(g_pkg_status_p->mm.packet_total_num) == \
                    MY_HTONS(g_pkg_status_p->mm.packet_index)){
                g_pkg_status_p->mm_data_trans_waiting = 0;
                printf("transmit one file over!\n");
                delete_mm_resource(MY_HTONL(g_pkg_status_p->mm.id));
                //display_mm_resource();
            }else{
                sample_send_image(pHeader->device_id);
            }
        }else{
            printf("recv package index error!\n");
        }
    }
    return 0;
}

static int32_t sample_send_image(uint8_t devid)
{
    int ret=0;
    int offset=0;
    uint32_t retval = 0;
    uint8_t *data=NULL;
    uint8_t *txbuf=NULL;
    uint32_t datalen=0;
    uint32_t txbuflen=0;

    size_t filesize = 0;
    FILE *fp = NULL;
    sample_prot_header *pSend = NULL;

    datalen = IMAGE_SIZE_PER_PACKET + \
        sizeof(sample_prot_header) + sizeof(sample_mm_ack) + 64;
    txbuflen = (IMAGE_SIZE_PER_PACKET + \
            sizeof(sample_prot_header) + sizeof(sample_mm_ack) + 64)*2;

    data = (uint8_t *)malloc(datalen);
    if(!data)
    {
        perror("send image malloc");
        retval = 1;
        goto out;
    }
    txbuf = (uint8_t *)malloc(txbuflen);
    if(!txbuf)
    {
        perror("send image malloc");
        retval = 1;
        goto out;
    }

    pSend = (sample_prot_header *) txbuf;

    mmid_to_filename(MY_HTONL(g_pkg_status_p->mm.id), g_pkg_status_p->mm.type, g_pkg_status_p->filepath);

    fp = fopen(g_pkg_status_p->filepath, "rb");
    if(fp ==NULL)
    {
        printf("open %s fail\n", g_pkg_status_p->filepath);
        retval = -1;
        goto out;
    }

    memcpy(data, &g_pkg_status_p->mm, sizeof(g_pkg_status_p->mm));
    offset = MY_HTONS(g_pkg_status_p->mm.packet_index) * IMAGE_SIZE_PER_PACKET;
    fseek(fp, offset, SEEK_SET);
    ret = fread(data + sizeof(g_pkg_status_p->mm), 1, IMAGE_SIZE_PER_PACKET, fp);
    fclose(fp);
    if(ret>0)
    {
        sample_assemble_msg_to_push(pSend,SAMPLE_DEVICE_ID_ADAS, SAMPLE_CMD_UPLOAD_MM_DATA, \
                data, (sizeof(g_pkg_status_p->mm) + ret));
        WSI_DEBUG("send...[%d/%d]\n", MY_HTONS(g_pkg_status_p->mm.packet_total_num),\
                MY_HTONS(g_pkg_status_p->mm.packet_index));
        g_pkg_status_p->mm.packet_index = MY_HTONS(MY_HTONS(g_pkg_status_p->mm.packet_index) + 1);
    }
    else//end and clear
    {
        printf("read file ret <=0\n");
        perror("error: read image file:");
    }

out:
    if(data)
        free(data);
    if(txbuf)
        free(txbuf);

    return retval;
}

void write_real_time_data(sample_prot_header *pHeader, int32_t len)
{
    if(len == sizeof(sample_prot_header) + 1 + sizeof(real_time_data))
    {
        RealTimeDdata_process((real_time_data *)(pHeader+1), WRITE_REAL_TIME_MSG);
    }
    else
    {
        printf("recv cmd:0x%x, data len maybe error!\n", pHeader->cmd);
    }
}

void do_factory_reset(uint8_t dev_id)
{
    if(dev_id == SAMPLE_DEVICE_ID_ADAS){
        set_adas_para_setting_default();
        write_local_adas_para_file(LOCAL_ADAS_PRAR_FILE);

    }else if(dev_id == SAMPLE_DEVICE_ID_DMS){
        set_dms_para_setting_default();
        write_local_dms_para_file(LOCAL_DMS_PRAR_FILE);
    }
}

void recv_para_setting(sample_prot_header *pHeader, int32_t len)
{
    adas_para_setting recv_adas_para;
    dms_para_setting recv_dms_para;
    uint8_t ack = 0;
    char cmd[100];
    uint8_t txbuf[128] = {0};
    sample_prot_header *pSend = (sample_prot_header *) txbuf;
    int ret = -1;

    if(pHeader->device_id == SAMPLE_DEVICE_ID_ADAS){

        if(len == sizeof(sample_prot_header) + 1 + sizeof(recv_adas_para)){
            memcpy(&recv_adas_para, pHeader+1, sizeof(recv_adas_para));

            //大端传输
            recv_adas_para.auto_photo_time_period = MY_HTONS(recv_adas_para.auto_photo_time_period);
            recv_adas_para.auto_photo_distance_period = MY_HTONS(recv_adas_para.auto_photo_distance_period);
            write_dev_para(&recv_adas_para, SAMPLE_DEVICE_ID_ADAS);

            printf("recv adas para...\n");
            print_adas_para(&recv_adas_para);

            ret = write_local_adas_para_file(LOCAL_ADAS_PRAR_FILE);
        }else{
            printf("recv cmd:0x%x, adas data len=%d maybe error!\n",len, pHeader->cmd);
        }
    }else if(pHeader->device_id == SAMPLE_DEVICE_ID_DMS){
        if(len == sizeof(sample_prot_header) + 1 + sizeof(recv_dms_para)){
            memcpy(&recv_dms_para, pHeader+1, sizeof(recv_dms_para));



            //大端传输
            recv_dms_para.auto_photo_time_period = MY_HTONS(recv_dms_para.auto_photo_time_period);
            recv_dms_para.auto_photo_distance_period = MY_HTONS(recv_dms_para.auto_photo_distance_period);
            recv_dms_para.Smoke_TimeIntervalThreshold = MY_HTONS(recv_dms_para.Smoke_TimeIntervalThreshold);
            recv_dms_para.Call_TimeIntervalThreshold = MY_HTONS(recv_dms_para.Call_TimeIntervalThreshold);

            printf("recv dms para...\n");
            print_dms_para(&recv_dms_para);

            write_dev_para(&recv_dms_para, SAMPLE_DEVICE_ID_DMS);
            ret = write_local_dms_para_file(LOCAL_DMS_PRAR_FILE);

        }else{
            printf("recv cmd:0x%x, dms data len=%d maybe error!\n", len, pHeader->cmd);
        }
    }

#if 0
    printf("start to kill algo!\n");
    //set alog detect.flag
    system("busybox killall -9 split_detect"); //killall
    sprintf(cmd, "busybox sed -i 's/^.*--output_lane_info_speed_thresh.*$/--output_lane_info_speed_thresh=%d/' \
            /data/xiao/install/detect.flag",recv_para.warning_speed_val);
    ret = system(cmd);
    printf("setting para ret = %d\n", ret);
    usleep(100000);
    printf("restart algo!\n");
    system("/data/algo.sh & >/dev/null");//restart
#endif

    //设置参数成功
    if(!ret){
        ack = 0;
        sample_assemble_msg_to_push(pSend, pHeader->device_id, SAMPLE_CMD_SET_PARAM, (uint8_t*)&ack, 1);
    }else{
        ack = 1;
        sample_assemble_msg_to_push(pSend, pHeader->device_id, SAMPLE_CMD_SET_PARAM, \
                (uint8_t*)&ack, 1);
    }
}

void send_para_setting(sample_prot_header *pHeader, int32_t len)
{
    adas_para_setting send_adas_para;
    dms_para_setting send_dms_para;
    uint8_t txbuf[256] = {0};
    sample_prot_header *pSend = (sample_prot_header *) txbuf;

    if(len == sizeof(sample_prot_header) + 1)
    {
        if(pHeader->device_id == SAMPLE_DEVICE_ID_ADAS){
            read_dev_para(&send_adas_para, SAMPLE_DEVICE_ID_ADAS);
            send_adas_para.auto_photo_time_period = MY_HTONS(send_adas_para.auto_photo_time_period);
            send_adas_para.auto_photo_distance_period = MY_HTONS(send_adas_para.auto_photo_distance_period);
            sample_assemble_msg_to_push(pSend,pHeader->device_id, SAMPLE_CMD_GET_PARAM, \
                    (uint8_t*)&send_adas_para, sizeof(send_adas_para));

        }else if(pHeader->device_id == SAMPLE_DEVICE_ID_DMS){
            read_dev_para(&send_dms_para, SAMPLE_DEVICE_ID_DMS);
            send_dms_para.auto_photo_time_period = MY_HTONS(send_dms_para.auto_photo_time_period);
            send_dms_para.auto_photo_distance_period = MY_HTONS(send_dms_para.auto_photo_distance_period);
            send_dms_para.Smoke_TimeIntervalThreshold = MY_HTONS(send_dms_para.Smoke_TimeIntervalThreshold);
            send_dms_para.Call_TimeIntervalThreshold = MY_HTONS(send_dms_para.Call_TimeIntervalThreshold);
            sample_assemble_msg_to_push(pSend, pHeader->device_id, SAMPLE_CMD_GET_PARAM, \
                    (uint8_t*)&send_dms_para, sizeof(send_dms_para));
        }
    }else{
        printf("recv cmd:0x%x, data len maybe error!\n", pHeader->cmd);
    }
}

void recv_warning_ack(sample_prot_header *pHeader, int32_t len)
{
    SendStatus pkg;

    if(len == sizeof(sample_prot_header) + 1){
        printf("push warning ack!\n");
        notice_ack_msg();

    }else{
        printf("recv cmd:0x%x, data len maybe error!\n", pHeader->cmd);
    }
}

void send_work_status_req_ack(sample_prot_header *pHeader, int32_t len)
{
    module_status module;
    uint8_t txbuf[256] = {0};
    sample_prot_header *pSend = (sample_prot_header *) txbuf;

    memset(&module, 0, sizeof(module));

    if(len == sizeof(sample_prot_header) + 1){
        module.work_status = MODULE_WORKING;
        sample_assemble_msg_to_push(pSend, pHeader->device_id, SAMPLE_CMD_REQ_STATUS, \
                (uint8_t*)&module, sizeof(module));
    }else{
        printf("recv cmd:0x%x, data len maybe error!\n", pHeader->cmd);
    }
}
void send_work_status(uint8_t devid)
{
    module_status module;
    uint8_t txbuf[256] = {0};
    sample_prot_header *pSend = (sample_prot_header *) txbuf;

    memset(&module, 0, sizeof(module));
    module.work_status = MODULE_WORKING;
    sample_assemble_msg_to_push(pSend, devid, SAMPLE_CMD_REQ_STATUS, \
            (uint8_t*)&module, sizeof(module));
}

void recv_upload_status_cmd_ack(sample_prot_header *pHeader, int32_t len)
{
    SendStatus pkg;

    if(len == sizeof(sample_prot_header) + 1){
        notice_ack_msg();
    }else{
        printf("recv cmd:0x%x, data len maybe error!\n", pHeader->cmd);
    }
}

typedef struct _file_trans_msg
{
    uint16_t    packet_num;
    uint16_t    packet_index;
} __attribute__((packed)) file_trans_msg;

#define UPGRADE_CMD_START       0x1
#define UPGRADE_CMD_CLEAN       0x2
#define UPGRADE_CMD_TRANS       0x3
#define UPGRADE_CMD_RUN         0x4
uint32_t get_sum(uint8_t *buf, int len)
{
    uint32_t sum = 0;
    int i=0;

    for(i=0; i<len; i++)
    {
        sum += buf[i];
    }
    return sum;
}
int recv_upgrade_file(sample_prot_header *pHeader, int32_t len)
{
    int ret;
    int fd;
    char cmd_rm_file[50];
    static uint32_t sum = 0;
    static uint32_t sum_new = 0;
    uint8_t data[4];
    uint8_t data_ack[6];
    uint8_t ack[2];
    int32_t datalen = 0;
    uint8_t *pchar=NULL;
    static uint32_t packet_num = 0;
    static uint32_t packet_index = 0;
    static uint32_t offset = 0;
    uint8_t     message_id = 0x03;
    file_trans_msg file_trans;
    unsigned char txbuf[256] = {0};
    sample_prot_header * pSend = (sample_prot_header *) txbuf;

    pchar = (uint8_t *)(pHeader+1);
    message_id = *pchar;

    printf("doing message_id 0x%02x\n", message_id);
    if(message_id == UPGRADE_CMD_START ||\
            message_id == UPGRADE_CMD_CLEAN ||\
            message_id == UPGRADE_CMD_RUN)
    {

        if(message_id == UPGRADE_CMD_CLEAN)
        {
            system(CLEAN_MPK);
            //do clean
        }
        if(message_id == UPGRADE_CMD_RUN)
        {
            //do run, do upgrade
            printf("exe new app...\n");
           // system(UPGRADE_FILE_CMD);
            //system("stop bootanim;echo 'restart...';/system/bin/main.sh");

            ack[0] = message_id;
            ack[1] = 0;
            sample_assemble_msg_to_push(pSend,pHeader->device_id, SAMPLE_CMD_UPGRADE, \
                    ack, sizeof(ack));

            system("touch /mnt/obb/restart");
            //system("/data/restart.sh &");
            //return 0;
        }
        ack[0] = message_id;
        ack[1] = 0;
        sample_assemble_msg_to_push(pSend,pHeader->device_id, SAMPLE_CMD_UPGRADE, \
                ack, sizeof(ack));
    }
    else if(message_id == UPGRADE_CMD_TRANS) //recv file
    {
        memcpy(&file_trans, pchar+1, sizeof(file_trans));
        packet_num = MY_HTONS(file_trans.packet_num);
        packet_index = MY_HTONS(file_trans.packet_index);

        if(packet_index == 0)
        {
            sprintf(cmd_rm_file, "rm %s",UPGRADE_FILE_PATH);
            system(cmd_rm_file);
            memcpy(&data, pchar+1+sizeof(file_trans), 4);

            //  sum = *(uint32_t *)&data[0];
            //sum = MY_HTONL(*(uint32_t *)&data[0]);
            sum = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];

            printf("recv sum = 0x%08x\n", sum);
        }
        else //recv file
        {
            memcpy(&file_trans, pchar+1, sizeof(file_trans));
            packet_num = MY_HTONS(file_trans.packet_num);
            packet_index = MY_HTONS(file_trans.packet_index);

            datalen = len - (sizeof(sample_prot_header) + 1 + 5);
            printf("recv [%d]/[%d], datalen = %d\n", packet_num, packet_index, datalen);

            fd = open(UPGRADE_FILE_PATH, O_RDWR|O_CREAT, 0644);
            lseek(fd, offset, SEEK_SET);
            ret = write(fd, pchar+5, datalen);
            close(fd);

            if(ret > 0)
                offset += ret;
            sum_new += get_sum((uint8_t *)pchar+5, datalen);
            if(packet_index+1 == packet_num) //the last packet , packet 0 is sum, no data!
            {
                printf("sumnew = 0x%08x, sum=0x%08x\n", sum_new, sum);
                if(sum_new == sum)
                {
                    printf("sun check ok!\n");
                    data_ack[5] = 0;
                    printf("upgrade...\n");
                    system(UPGRADE_FILE_CMD);
                }
                else
                {
                    printf("sun check err!\n");
                    data_ack[5] = 1;
                }
                memcpy(data_ack, pchar, 5);
                sample_assemble_msg_to_push(pSend,pHeader->device_id, SAMPLE_CMD_UPGRADE, \
                        data_ack, sizeof(data_ack));
                return 0;
            }
        }
        memcpy(data_ack, pchar, 5);
        data_ack[5] = 0;
        sample_assemble_msg_to_push(pSend,pHeader->device_id, SAMPLE_CMD_UPGRADE, \
                data_ack, sizeof(data_ack));
    }
    else
        ;

    return 0;
}
static int32_t sample_on_cmd(sample_prot_header *pHeader, int32_t len)
{
    ptr_queue_node msgack;
    uint16_t serial_num = 0;

    sample_dev_info dev_info = {
        15, "MINIEYE",
        15, "M4",
        15, "1.0.0.1",
        15, "1.0.1.9", //soft version
        15, "0xF0321564",
        15, "SAMPLE",
    };
    uint8_t txbuf[128] = {0};
    sample_prot_header *pSend = (sample_prot_header *) txbuf;

#if defined ENABLE_ADAS
    if(pHeader->device_id != SAMPLE_DEVICE_ID_ADAS &&\
            pHeader->device_id != SAMPLE_DEVICE_ID_BRDCST)
        return 0;

#elif defined ENABLE_DMS
    if((pHeader->device_id != SAMPLE_DEVICE_ID_DMS) && (pHeader->device_id != SAMPLE_DEVICE_ID_BRDCST)){
        return 0;
    }
#else
    printf("no defien device.\n");
    return 0;
#endif
    serial_num = MY_HTONS(pHeader->serial_num);
    do_serial_num(&serial_num, RECORD_RECV_NUM);

    printf("------cmd = 0x%x------\n", pHeader->cmd);
    switch (pHeader->cmd)
    {
        case SAMPLE_CMD_QUERY:
            sample_assemble_msg_to_push(pHeader,pHeader->device_id, SAMPLE_CMD_QUERY, NULL, 0);
            break;

        case SAMPLE_CMD_FACTORY_RESET:
            sample_assemble_msg_to_push(pHeader,pHeader->device_id, SAMPLE_CMD_FACTORY_RESET, NULL, 0);
            do_factory_reset(pHeader->device_id);
            break;

        case SAMPLE_CMD_SPEED_INFO: //不需要应答
            write_real_time_data(pHeader, len);
            break;

        case SAMPLE_CMD_DEVICE_INFO:
            sample_assemble_msg_to_push(pSend,pHeader->device_id, SAMPLE_CMD_DEVICE_INFO,
                    (uint8_t*)&dev_info, sizeof(dev_info));
            break;

        case SAMPLE_CMD_UPGRADE:
            recv_upgrade_file(pHeader, len);
            break;

        case SAMPLE_CMD_GET_PARAM:
            send_para_setting(pHeader, len);
            break;

        case SAMPLE_CMD_SET_PARAM:
            recv_para_setting(pHeader, len);
            break;

        case SAMPLE_CMD_WARNING_REPORT: //recv warning ack
            recv_warning_ack(pHeader, len);
            break;

        case SAMPLE_CMD_REQ_STATUS: 
            send_work_status_req_ack(pHeader, len);
            break;

        case SAMPLE_CMD_UPLOAD_STATUS: //主动上报状态后，接收到ack
            recv_upload_status_cmd_ack(pHeader, len);
            break;

        case SAMPLE_CMD_REQ_MM_DATA:
            //发送多媒体请求应答
            send_mm_req_ack(pHeader,len);
            break;

        case SAMPLE_CMD_UPLOAD_MM_DATA:
            recv_ack_and_send_image(pHeader, len);
            break;

        case SAMPLE_CMD_SNAP_SHOT:
            WSI_DEBUG("------snap shot----------\n");
            send_snap_shot_ack(pHeader, len);
            do_snap_shot();
            break;

        default:
            printf("****************UNKNOW frame!*************\n");
            printbuf((uint8_t *)pHeader, len);
            break;
    }
    return 0;
}

static int try_connect()
{
#define HOST_SERVER_PORT (8888)

    int sock;
    int32_t ret = 0;
    int enable = 1;
    const char *server_ip = "192.168.100.100";
    //const char *server_ip = "192.168.100.105";
    struct sockaddr_in host_serv_addr;
    socklen_t optlen;
    int bufsize = 0;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Create socket failed %s\n", strerror(errno));
        return -2;
    }
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    bufsize = 0;
    optlen = sizeof(bufsize);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufsize, &optlen);
    printf("get recv buf size = %d\n", bufsize);
    getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufsize, &optlen);
    printf("get send buf size = %d\n", bufsize);
    //int setsockopt(int sockfd, int level, int optname,const void *optval, socklen_t optlen);

    printf("set buf size = %d\n", bufsize);
    bufsize = 64*1024;
    optlen = sizeof(bufsize);
    ret = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufsize, optlen);
    if(ret == -1)
    {
        printf("%s:%d error\n", __FILE__, __LINE__);
        return -1;
    }
    bufsize = 64*1024;
    optlen = sizeof(bufsize);
    ret = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufsize, optlen);
    if(ret == -1)
    {
        printf("%s:%d error\n", __FILE__, __LINE__);
        return -1;
    }

    bufsize = 0;
    optlen = sizeof(bufsize);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufsize, &optlen);
    printf("get recv buf size = %d\n", bufsize);
    getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufsize, &optlen);
    printf("get send buf size = %d\n", bufsize);

    memset(&host_serv_addr, 0, sizeof(host_serv_addr));
    host_serv_addr.sin_family = AF_INET;
    host_serv_addr.sin_port   = MY_HTONS(HOST_SERVER_PORT);

    ret = inet_aton(server_ip, &host_serv_addr.sin_addr);
    if (0 == ret) {
        printf("inet_aton failed %d %s\n", ret, strerror(errno));
        return -2;
    }

    while(1)
    {

        if( 0 == connect(sock, (struct sockaddr *)&host_serv_addr, sizeof(host_serv_addr)))
        {
            printf("connect ok!\n");
            return sock;
        }
        else
        {
            sleep(1);
            printf("try connect!\n");
        }
    }
}

void *pthread_tcp_process(void *para)
{
#define TCP_READ_BUF_SIZE (64*1024)
#define RECV_HOST_DATA_BUF_SIZE (128*1024)
    int32_t ret = 0;
    int i=0;
    static int tcprecvcnt = 0;
    uint8_t *readbuf = NULL;
    uint8_t *msgbuf = NULL;

    prctl(PR_SET_NAME, "tcp_process");
    send_stat_pkg_init();

    msgbuf = (uint8_t *)malloc(RECV_HOST_DATA_BUF_SIZE);
    if(!msgbuf)
    {
        perror("parse_host_cmd malloc");
        return NULL;
    }
    readbuf = (uint8_t *)malloc(TCP_READ_BUF_SIZE);
    if(!readbuf){
        perror("tcp readbuf malloc");
        goto out;
    }

connect_again:
    hostsock = try_connect();
    if(hostsock < 0)
        goto connect_again;

#if defined ENABLE_ADAS
    send_work_status(SAMPLE_DEVICE_ID_ADAS);
#elif defined ENABLE_DMS
    send_work_status(SAMPLE_DEVICE_ID_DMS);
#endif

    while (!force_exit) {
        ret = read(hostsock, readbuf, TCP_READ_BUF_SIZE);
        if (ret <= 0) {
            printf("read failed %d %s\n", ret, strerror(errno));
            close(hostsock);
            hostsock = -1;
            goto connect_again;

            //continue;
        }else{//write to buf
            //MY_DEBUG("recv raw cmd, tcprecvcnt = %d:\n", tcprecvcnt++);
            //printbuf(readbuf, ret);
            i=0;
            while(ret--){
                parse_cmd(&readbuf[i++], msgbuf);
            }
        }
    }
out:
    if(readbuf)
        free(readbuf);
    if(msgbuf)
        free(msgbuf);

    if(hostsock>0)
        close(hostsock);
    pthread_exit(NULL);
}
static char get_head = 0;
static char got_esc_char = 0;
static int cnt = 0;

void clear_frame_flag()
{
        //clear
        get_head = 0;
        got_esc_char = 0;
        cnt = 0;
}

static uint32_t unescaple_msg(uint8_t *buf, uint8_t *msg, int msglen)
{
    uint8_t ch = 0;
    uint32_t framelen = 0;
    ch = buf[0];

    if(cnt+1 > msglen){
        printf("error: recv msg too long\n");
        clear_frame_flag();
        return 0;
    }
    //printf("ch = 0x%x\n", buf[0]);
    //not recv head
    if(!get_head){
        if((ch == SAMPLE_PROT_MAGIC) && (cnt == 0)){
            msg[cnt] = SAMPLE_PROT_MAGIC;
            cnt++;
            get_head = 1;
            return 0;
        }
    }else{//recv head
        if((ch == SAMPLE_PROT_MAGIC) && (cnt > 0)) {//get tail
            if(cnt < 6){//maybe error frame, as header, restart
                cnt = 0;
                msg[cnt] = SAMPLE_PROT_MAGIC;
                cnt++;
                get_head = 1;

                //clear_frame_flag();
                return 0;

            }else{ //success
                msg[cnt] = SAMPLE_PROT_MAGIC;
                get_head = 0;//over
                cnt++;
                framelen = cnt;

                clear_frame_flag();
                return framelen;
            }
        }else{//get text
            if((ch == SAMPLE_PROT_ESC_CHAR) && !got_esc_char){//need deal
                got_esc_char = 1;
                msg[cnt] = ch;
                cnt++;
            }else if(got_esc_char && (ch == 0x02)){
                msg[cnt-1] = SAMPLE_PROT_MAGIC;
                got_esc_char = 0;
            }else if(got_esc_char && (ch == 0x01)){
                msg[cnt-1] = SAMPLE_PROT_ESC_CHAR;
                got_esc_char = 0;
            }else{
                msg[cnt] = ch;
                cnt++;
                got_esc_char = 0;
            }
        }
    }
    return 0;
}

void parse_cmd(uint8_t *buf, uint8_t *msgbuf)
{
    uint32_t ret = 0;
    uint8_t sum = 0;
    uint32_t framelen = 0;
    sample_prot_header *pHeader = NULL;
    pHeader = (sample_prot_header *) msgbuf;
    ret = unescaple_msg(buf, msgbuf, RECV_HOST_DATA_BUF_SIZE);
    if(ret>0){
        framelen = ret;
        printf("recv framelen = %d\n", framelen);
        sum = sample_calc_sum(pHeader, framelen);
        if (sum != pHeader->checksum) {
            printf("Checksum missmatch calcated: 0x%02hhx != 0x%2hhx\n",
                    sum, pHeader->checksum);
        }else{
            sample_on_cmd(pHeader, framelen);
        }
    }
}

#define SNAP_SHOT_CLOSE            0
#define SNAP_SHOT_BY_TIME          1
#define SNAP_SHOT_BY_DISTANCE      2
void *pthread_snap_shot(void *p)
{
#ifdef ENABLE_ADAS
    adas_para_setting tmp;
    uint8_t para_type = SAMPLE_DEVICE_ID_ADAS;
#else
    dms_para_setting tmp;
    uint8_t para_type = SAMPLE_DEVICE_ID_DMS;
#endif
    real_time_data rt_data;;
    uint32_t mileage_last = 0;


    prctl(PR_SET_NAME, "pthread_snap");

    while(!force_exit)
    {

        read_dev_para(&tmp, para_type);
#if 1
        if(tmp.auto_photo_mode == SNAP_SHOT_BY_TIME){
            printf("auto snap shot!\n");
            if(tmp.auto_photo_time_period != 0)
                do_snap_shot();
            sleep(tmp.auto_photo_time_period);
#else
        if(1){
            sleep(5);
            if(1)
            {
                printf("auto snap shot!\n");
                do_snap_shot();
            }
#endif
        }else if(tmp.auto_photo_mode == SNAP_SHOT_BY_DISTANCE){
            RealTimeDdata_process(&rt_data, READ_REAL_TIME_MSG);
            if((rt_data.mileage - mileage_last)*100 >= tmp.auto_photo_distance_period){
                if(mileage_last != 0){
                    printf("snap by mileage!\n");
                    do_snap_shot();
                }
                mileage_last = rt_data.mileage; //单位是0.1km
            }else{
                sleep(1);
            }
        }else{
            sleep(2);
        }
    }
    pthread_exit(NULL);
}

void *pthread_req_cmd_process(void *para)
{
    uint32_t mm_id = 0;
    uint8_t mm_type = 0;
    uint8_t warn_type = 0;
    uint32_t filesize = 0;
    send_mm_info send_mm;
    send_mm_info *send_mm_ptr = &send_mm;
    struct timeval req_time;  
    int ret = 0;

    uint8_t recv_time[6];
    get_local_time(recv_time);

    prctl(PR_SET_NAME, "cache_GetVideocmd");
    gettimeofday(&test_time, NULL);


    while(1)
    {
        if(!pull_mm_req_cmd_queue(&send_mm))
        {
            printf("pull mm_info!\n");

            gettimeofday(&req_time, NULL);
            while(1){

                mm_id = MY_HTONL(send_mm_ptr->id);
                mm_type = send_mm_ptr->type;
                ret = find_local_image_name(mm_type, mm_id,  g_pkg_status_p->filepath, &filesize);
                if(ret != 0)
                {
                    if(timeout_trigger(&req_time, 10*1000))//timeout
                    {
                        g_pkg_status_p->mm_data_trans_waiting = 0;
                        break;
                    }

                    printf("try find mm file!\n");
                    sleep(1);
                    continue;
                }

                //记录当前包的信息, 发送应答
                g_pkg_status_p->mm.type = mm_type;
                g_pkg_status_p->mm.id = send_mm_ptr->id;
                g_pkg_status_p->mm.packet_index = 0;
                g_pkg_status_p->mm.packet_total_num = MY_HTONS((filesize + IMAGE_SIZE_PER_PACKET - 1)/IMAGE_SIZE_PER_PACKET);

                //send first package
                printf("send first package!\n");
                sample_send_image(send_mm_ptr->devid);
                break;

            }
        }else{

            usleep(20000);
        }
    }
}

