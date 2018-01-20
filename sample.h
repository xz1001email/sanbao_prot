#ifndef  __SAMPLE_H__
#define __SAMPLE_H__
#include <stdio.h>
#include <stdint.h>

#include <queue>


#define SANBAO_VERSION 0x01
#define VENDOR_ID 0x1234

#define MM_ID 0x911

#define MM_PHOTO 0
#define MM_AUDIO 1
#define MM_VEDIO 2

#define IMAGE_CACHE_NUM     1


#define MESSAGE_CAN700	"output.can.0x700"
#define MESSAGE_CAN760	"output.can.0x760"


#define SAMPLE_DEVICE_ID_BRDCST         (0x0)
#define SAMPLE_DEVICE_ID_ADAS           (0x64)
#define SAMPLE_DEVICE_ID_DSM            (0x65)
#define SAMPLE_DEVICE_ID_TPMS           (0x66)
#define SAMPLE_DEVICE_ID_BSD            (0x67)

#define SAMPLE_CMD_QUERY                (0x2F)
#define SAMPLE_CMD_FACTORY_RESET        (0x30)
#define SAMPLE_CMD_SPEED_INFO           (0x31)
#define SAMPLE_CMD_DEVICE_INFO          (0x32)
#define SAMPLE_CMD_UPGRADE              (0x33)
#define SAMPLE_CMD_GET_PARAM            (0x34)
#define SAMPLE_CMD_SET_PARAM            (0x35)
#define SAMPLE_CMD_WARNING_REPORT       (0x36)
#define SAMPLE_CMD_REQ_STATUS           (0x37)
#define SAMPLE_CMD_UPLOAD_STATUS        (0x38)
#define SAMPLE_CMD_REQ_MM_DATA          (0x50)
#define SAMPLE_CMD_UPLOAD_MM_DATA       (0x51)
#define SAMPLE_CMD_UPLOAD_MM_DATA_ACK   (0x51)

#define SAMPLE_PROT_MAGIC               (0x7E)
#define SAMPLE_PROT_ESC_CHAR            (0x7D)
typedef struct _sample_prot_heade
{
    uint8_t     magic;
    uint8_t     checksum;
//    uint16_t    version;//modify
    uint16_t    serial_num;
    uint16_t    vendor_id;
    uint8_t     device_id;
    uint8_t     cmd;
    //uint8_t     payload[n];
    //uint8_t     magic1;
} __attribute__((packed)) sample_prot_header;

typedef struct _sample_mm_info
{
    uint8_t  type;
    uint32_t id;
} __attribute__((packed)) sample_mm_info;

typedef struct _sample_dev_info
{
    uint8_t     vendor_name_len;
    uint8_t     vendor_name[15];
    uint8_t     prod_code_len;
    uint8_t     prod_code[15];
    uint8_t     hw_ver_len;
    uint8_t     hw_ver[15];
    uint8_t     sw_ver_len;
    uint8_t     sw_ver[15];
    uint8_t     dev_id_len;
    uint8_t     dev_id[15];
    uint8_t     custom_code_len;
    uint8_t     custom_code[15];
} __attribute__((packed)) sample_dev_info;

#define SW_STATUS_BEGIN (0x0)
#define SW_STATUS_END   (0x1)
#define SW_STATUS_EVENT (0x10)

#define SW_TYPE_FCW     (0x1)
#define SW_TYPE_LDW     (0x2)
#define SW_TYPE_HW      (0x3)
#define SW_TYPE_PCW     (0x4)
#define SW_TYPE_FLC     (0x5)
#define SW_TYPE_TSRW    (0x6)
#define SW_TYPE_TSR     (0x10)
#define SW_TYPE_SNAP    (0x11)
#define SW_TSR_TYPE_SPEED   (0x1)
#define SW_TSR_TYPE_HIGHT   (0x2)
#define SW_TSR_TYPE_WEIGHT  (0x3)
typedef struct _sample_warning
{
    uint8_t     reserve0;
    uint8_t     status;
    uint8_t     type;
    uint8_t     reserve1;
    uint8_t     tsr_type;
    uint8_t     tsr_data;
    uint8_t     reserve2[2];
    uint8_t     mm_count;
} __attribute__((packed)) sample_warning;

typedef struct _sample_mm
{
    uint8_t     req_type;
    uint32_t    mm_id;
    uint16_t    packet_total_num;
    uint16_t    packet_index;
} __attribute__((packed)) sample_mm;


typedef struct _sample_mm_ack
{
    uint8_t     req_type;
    uint32_t    mm_id;
    uint16_t    packet_total_num;
    uint16_t    packet_index;
    uint8_t     ack;
} __attribute__((packed)) sample_mm_ack;

typedef struct __MECANWarningMessage {
    //#ifdef BIG_ENDIAN
#if 0
    uint8_t     byte0_resv:5;
    uint8_t     sound_type:3;

    uint8_t     byte1_resv0:2;
    uint8_t     zero_speed:1;
    uint8_t     byte1_resv1:5;

    uint8_t     headway_measurement:7;
    uint8_t     headway_valid:1;

    uint8_t     byte3_resv:7;
    uint8_t     no_error:1;

    uint8_t     byte4_resv:4;
    uint8_t     fcw_on:1;
    uint8_t     right_ldw:1;
    uint8_t     left_ldw:1;
    uint8_t     ldw_off:1;

    uint8_t     byte5_resv;
    uint8_t     byte6_resv;

    uint8_t     byte7_resv:6;
    uint8_t     headway_warning_level:2;
#else /*Little Endian*/
    uint8_t     sound_type:3;
    uint8_t     time_indicator:2;
    uint8_t     byte0_resv:3;

    uint8_t     byte1_resv1:5;
    uint8_t     zero_speed:1;
    uint8_t     byte1_resv0:2;

    uint8_t     headway_valid:1;
    uint8_t     headway_measurement:7;

    uint8_t     no_error:1;
    uint8_t     error_code:7;

    uint8_t     ldw_off:1;
    uint8_t     left_ldw:1;
    uint8_t     right_ldw:1;
    uint8_t     fcw_on:1;
    uint8_t     byte4_resv:2;
    uint8_t     maintenanc:1;
    uint8_t     failsafe:1;

    uint8_t     byte5_resv0:1;
    uint8_t     peds_fcw:1;
    uint8_t     peds_in_dz:1;
    uint8_t     byte5_resv1:2;
    uint8_t     tamper_alert:1;
    uint8_t     byte5_resv2:1;
    uint8_t     tsr_enable:1;

    uint8_t     tsr_warning_level:3;
    uint8_t     byte6_resv:5;

    uint8_t     headway_warning_level:2;
    uint8_t     hw_repeatable_enable:1;
    uint8_t     byte7_resv:5;
#endif
} __attribute__((packed)) MECANWarningMessage;



typedef struct __car_status {
    uint8_t		acc:1;
    uint8_t     left_signal:1;
    uint8_t     right_signal:1;
    uint8_t     wipers:1;
    uint8_t     inster:1;
    uint8_t     brakes:1;

    uint16_t     byte_resv:10;

} __attribute__((packed)) car_status_s;

typedef struct __warningtext {

    uint32_t	warning_id;
    uint8_t		start_flag;
    uint8_t		sound_type;
    uint8_t		forward_car_speed;
    uint8_t		forward_car_Distance;
    uint8_t		ldw_type;
    uint8_t		load_type;
    uint8_t		load_data;
    uint8_t		car_speed;
    uint16_t	high;
    uint32_t	altitude;
    uint32_t	longitude;
    uint8_t		time[6];
    uint8_t		mm_num;
    car_status_s	car_status;	
    sample_mm_info mm;


} __attribute__((packed)) warningtext;


typedef struct __car_info {
//#ifdef BIG_ENDIAN

#if 0
    uint8_t     byte0_resv1:1;
    uint8_t     byte0_resv0:1;
    uint8_t     high_beam:1;
    uint8_t     low_beam:1;
    uint8_t     wipers:1;
    uint8_t     right_signal:1;
    uint8_t     left_signal:1;
    uint8_t     brakes:1;

    uint8_t     speed_aval:1;
    uint8_t     byte1_resv1:1;
    uint8_t     high_beam_aval:1;
    uint8_t     low_beam_aval:1;
    uint8_t     wipers_aval:1;
    uint8_t     byte1_resv0:3;

    uint8_t     speed;

#else /*Little Endian*/
    uint8_t     brakes:1;
    uint8_t     left_signal:1;
    uint8_t     right_signal:1;
    uint8_t     wipers:1;
    uint8_t     low_beam:1;
    uint8_t     high_beam:1;
    uint8_t     byte0_resv0:1;
    uint8_t     byte0_resv1:1;

    uint8_t     byte1_resv0:3;
    uint8_t     wipers_aval:1;
    uint8_t     low_beam_aval:1;
    uint8_t     high_beam_aval:1;
    uint8_t     byte1_resv1:1;
    uint8_t     speed_aval:1;

    uint8_t     speed;

#endif
} __attribute__((packed)) car_info;



/**********************can message*****************************/
typedef struct _can_struct{
    uint8_t warning[8];
    char source[20];
    char time[20];
    char topic[20];
}can_data_type;

#define HW_LEVEL_NO_CAR     (0)
#define HW_LEVEL_WHITE_CAR  (1)
#define HW_LEVEL_RED_CAR    (2)

#define SOUND_TYPE_SILENCE  (0)
#define SOUND_TYPE_LLDW     (1)
#define SOUND_TYPE_RLDW     (2)
#define SOUND_TYPE_HW       (3)
#define SOUND_TYPE_TSR      (4)
#define SOUND_TYPE_VB       (5)
#define SOUND_TYPE_FCW_PCW  (6)

#define MINIEYE_WARNING_CAN_ID  (0x700)
#define MINIEYE_CAR_INFO_CAN_ID (0x760)



/*************set para*********************/
typedef struct __CAN_PARA {
    //#ifdef BIG_ENDIAN
#if 0

#else /*Little Endian*/   //a:0 a:1 ...
    uint8_t     voice;
    uint8_t     auto_photograph;
    uint16_t    auto_photograph_time_gap;
    uint16_t    auto_photograph_distance_gap;
    uint8_t     sigle_photograph_num;
    uint8_t     sigle_photograph_time_gap;

    uint8_t     photo_resolution;
    uint8_t     vedio_resolution;
    uint32_t    warning_enable;
    uint32_t    event_enable;
    uint8_t     resv0;
    uint8_t     threshold;
#endif

} __attribute__((packed)) can_para;


/**********queue and repeat_send struct****************/
#define PTR_QUEUE_BUF_SIZE   (1024 + 64)
#define PTR_QUEUE_BUF_CNT    (16)
#define UCHAR_QUEUE_SIZE    (2048)

typedef struct _ptr_queue_node{
    uint8_t cmd;
    uint8_t need_ack;

    int32_t len;
    uint8_t *buf;
} __attribute__((packed)) ptr_queue_node;

typedef struct _package_repeat_status{
#define REPEAT_SEND_TIMES_MAX   3

    bool mm_data_trans_waiting;
    uint8_t repeat_cnt;
    struct timeval msg_sendtime;
    bool start_wait_ack;
    ptr_queue_node msgsend;
} __attribute__((packed)) pkg_repeat_status;


void repeat_send_pkg_status_init();
void printbuf(uint8_t *buf, int len);
void *communicate_with_host(void *para);
void *parse_host_cmd(void *para);
int can_message_send(can_data_type *sourcecan);

#define DEBUG_BUF

#define DEBUG_G
#ifdef DEBUG_G 
//#define MY_DEBUG(format,...) printf("File: "__FILE__", Line: %05d:\n", __LINE__, ##__VA_ARGS__)
#define MY_DEBUG(format,...) printf(format, ##__VA_ARGS__)
#else
#define MY_DEBUG(format,...)
#endif



#endif
