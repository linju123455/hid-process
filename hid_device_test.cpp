#include "signal.h"
#include "unistd.h"
#include "cstring"
#include <fcntl.h>
#include "iostream"
#include <sys/stat.h>
#include <linux/usb/ch9.h>
#include <usbg/usbg.h>
#include <usbg/function/hid.h>
#include <thread>
#define VENDOR          0x0105
#define PRODUCT         0x1d67
usbg_state *s;
usbg_gadget *g;
int mouse_fd = 0;
int keyboard_fd = 0;

typedef enum {
	ev_Mouse_Undefined = -1,
	ev_Mouse_Absolute,
	ev_Mouse_Relative,
} MouseEvType;

typedef struct {
	MouseEvType         type;
	int32_t             x;
	int32_t             y;
	int32_t             mouseKey;
	int32_t             wheelValue;
} MouseEvMsg;

typedef struct {
	uint8_t*      keyboardMsg;
	MouseEvMsg      mouseMsg;
	int32_t         lightValue;
} EventMsg;

// 获取可用的 UDC 名称
static char* get_available_udc() {
    static char udc_name[256];
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("/sys/class/udc")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] != '.') {
                strcpy(udc_name, ent->d_name);
                closedir(dir);
                return udc_name;
            }
        }
        closedir(dir);
    }
    return NULL;
}

static int read_sys_led_status(const char* cmd, int* brightness)
{
	FILE *fp;
    char path[1035];

    // 使用 popen 打开文件并读取内容
    fp = popen(cmd, "r");
    if (fp == NULL) {
        printf("Failed to run command\n");
        return -1;
    }

    // 读取输出并存储到缓冲区
    if (fgets(path, sizeof(path), fp) != NULL) {
        *brightness = atoi(path);  // 将读取到的字符串转换为整数
    } else {
        printf("Failed to read brightness value\n");
		pclose(fp);
		return -1;
    }

    pclose(fp);

    return 0;
}

static const char* get_modifier_key_name(unsigned char keycode) {
    switch (keycode) {
        case 0x01: return "Left Ctrl";
        case 0x02: return "Left Shift";
        case 0x04: return "Left Alt";
        case 0x08: return "Left GUI (Windows)";
        case 0x10: return "Right Ctrl";
        case 0x20: return "Right Shift";
        case 0x40: return "Right Alt";
        case 0x80: return "Right GUI (Windows)";
        default: return "Unknown Modifier Key";
    }
}

static const char* get_key_name(unsigned char keycode) {
    switch (keycode) {
        case 0x04: return "a";
        case 0x05: return "b";
        case 0x06: return "c";
        case 0x07: return "d";
        case 0x08: return "e";
        case 0x09: return "f";
        case 0x0A: return "g";
        case 0x0B: return "h";
        case 0x0C: return "i";
        case 0x0D: return "j";
        case 0x0E: return "k";
        case 0x0F: return "l";
        case 0x10: return "m";
        case 0x11: return "n";
        case 0x12: return "o";
        case 0x13: return "p";
        case 0x14: return "q";
        case 0x15: return "r";
        case 0x16: return "s";
        case 0x17: return "t";
        case 0x18: return "u";
        case 0x19: return "v";
        case 0x1A: return "w";
        case 0x1B: return "x";
        case 0x1C: return "y";
        case 0x1D: return "z";
        case 0x1E: return "1";
        case 0x1F: return "2";
        case 0x20: return "3";
        case 0x21: return "4";
        case 0x22: return "5";
        case 0x23: return "6";
        case 0x24: return "7";
        case 0x25: return "8";
        case 0x26: return "9";
        case 0x27: return "0";
        case 0x29: return "Esc";
        case 0x2A: return "Backspace";
        case 0x2B: return "Tab";
        case 0x39: return "capsLock";
        // 添加更多按键代码和对应字符
        default: return "未知按键";
    }
}

static char mouse_abs_report_desc[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x03,        //     Usage Maximum (0x03)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x05,        //     Report Size (5)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0x00, 0x04,  //     Logical Maximum (1024)
    0x35, 0x00,        //     PHYSICAL_MINIMUM(0)
    0x46, 0x00, 0x04,  //     PHYSICAL_MAXIMUM(1024)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

static char mouse_rel_report_desc[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
0x09, 0x02,        // Usage (Mouse)
0xA1, 0x01,        // Collection (Application)
0x09, 0x01,        //   Usage (Pointer)
0xA1, 0x00,        //   Collection (Physical)
0x05, 0x09,        //     Usage Page (Button)
0x19, 0x01,        //     Usage Minimum (0x01)
0x29, 0x03,        //     Usage Maximum (0x03)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x01,        //     Logical Maximum (1)
0x75, 0x01,        //     Report Size (1)
0x95, 0x03,        //     Report Count (3)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x05,        //     Report Size (5)
0x95, 0x01,        //     Report Count (1)
0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
0x09, 0x30,        //     Usage (X)
0x09, 0x31,        //     Usage (Y)
// 0x15,0x81,      /*Logical Minimum(-127)*/
//     0x25,0x7f,      /*Logical Maximum(127)*/
//     0x75,0x08,      /*Report Size(8)*/
//     0x95,0x02,      /*Report Count(2)  */
0x16, 0x00, 0xF8,  //     Logical Minimum (-2047)
0x26, 0xFF, 0x07,  //     Logical Maximum (2047)
0x75, 0x0C,        //     Report Size (12)
0x95, 0x02,        //     Report Count (2)
0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x38,        //     Usage (Wheel)
0x15, 0x81,        //     Logical Minimum (-127)
0x25, 0x7F,        //     Logical Maximum (127)
0x75, 0x08,        //     Report Size (8)
0x95, 0x01,        //     Report Count (1)
0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
0x95, 0x01,        //     Report Count (1)
0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0xC0,              // End Collection

// 64 bytes

};


// static char keyboard_report_desc[] = {
//     // 前导部分
// 	0x05, 0x01,	        // USAGE_PAGE (Generic Desktop)
// 	0x09, 0x06,	        // USAGE (Keyboard)   
// 	0xa1, 0x01,	        // COLLECTION (Application) 

//     // LED输出部分（不占字节），用于用户控制LED灯
//     0x05, 0x08,         //   Usage Page (LEDs)
//     0x19, 0x01,         //   Usage Minimum (Num Lock)
//     0x29, 0x03,         //   Usage Maximum (Scroll Lock)
//     0x15, 0x00,         //   Logical Minimum (0)
//     0x25, 0x01,         //   Logical Maximum (1)
//     0x75, 0x01,         //   Report Size (1)
//     0x95, 0x03,         //   Report Count (3)
//     0x91, 0x02,         //   Output (Data,Var,Abs)
//     0x95, 0x05,         //   Report Count (5)
//     0x75, 0x01,         //   Report Size (1)
//     0x91, 0x01,         //   Output (Cnst,Var,Abs)

//     // 修饰键部分（1byte）
//     0x05, 0x07,         //   Usage Page (Keyboard)
//     0x19, 0xe0,         //   Usage Minimum (Left Control)
//     0x29, 0xe7,         //   Usage Maximum (Right GUI)
//     0x15, 0x00,         //   Logical Minimum (0)
//     0x25, 0x01,         //   Logical Maximum (1)
//     0x75, 0x01,         //   Report Size (1)
//     0x95, 0x08,         //   Report Count (8)
//     0x81, 0x02,         //   Input (Data,Var,Abs)

//     // 保留部分（1byte）
//     0x75, 0x08,         //   Report Size (8)
//     0x95, 0x01,         //   Report Count (1)
//     0x81, 0x01,         //   Input (Cnst,Array,Abs)

//     // 普通键部分（6byte）
//     0x05, 0x07,         //   Usage Page (Keyboard)
//     0x19, 0x00,         //   Usage Minimum (0)
//     0x29, 0x91,         //   Usage Maximum (145)
//     0x15, 0x00,         //   Logical Minimum (0)
//     0x26, 0xff, 0x00,   //   Logical Maximum (255)
//     0x75, 0x08,         //   Report Size (8)
//     0x95, 0x06,         //   Report Count (6)
//     0x81, 0x00,         //   Input (Data,Array,Abs)

//     // 结束符
//     0xc0                // End Collection
// };

static char keyboard_report_desc[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Cnst,Array,Abs)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x05,        //   Report Count (5)
    0x91, 0x02,        //   Output (Data,Var,Abs)
    0x75, 0x03,        //   Report Size (3)
    0x95, 0x01,        //   Report Count (1)
    0x91, 0x01,        //   Output (Cnst,Var,Abs)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x06,        //   Report Count (6)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x91,        //   Usage Maximum (145)
    0x81, 0x00,        //   Input (Data,Array,Abs)
    0xC0               // End Collection
};



void ProgExit(int signo)
{
    printf("come in prog exit\n");
    if (mouse_fd)
	    close(mouse_fd);
    if (keyboard_fd)
        close(keyboard_fd);
	usbg_rm_gadget(g, USBG_RM_RECURSE);
	usbg_cleanup(s);
    exit(0);
}

void errorCb(int signo)
{
	printf("come in errorCb\n");
	if (mouse_fd)
	    close(mouse_fd);
    if (keyboard_fd)
        close(keyboard_fd);
	usbg_rm_gadget(g, USBG_RM_RECURSE);
	usbg_cleanup(s);
    exit(-1);
}

bool CreateHIDDevice()
{
	usbg_config *c;
	usbg_function *f_hid_mouse;
    // usbg_function *f_hid_keyboard;
	int ret = -EINVAL;
	usbg_error usbg_ret;

	struct usbg_gadget_attrs g_attrs = {
		.bcdUSB = 0x0200,
		.bDeviceClass =	USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass = 0x00,
		.bDeviceProtocol = 0x00,
		.bMaxPacketSize0 = 8, /* Max allowed ep0 packet size */
		.idVendor = VENDOR,
		.idProduct = PRODUCT,
		.bcdDevice = 0x0200, /* Verson of device */
	};

	struct usbg_gadget_strs g_strs = {
		.manufacturer = "Fool Inc.", /* Manufacturer */
		.product = "Bar Gadget1", /* Product string */
		.serial = "01234567892", /* Serial number */
	};

	struct usbg_config_strs c_strs = {
		.configuration = "Configuration 1"
	};

	struct usbg_f_hid_attrs f_attrs_mouse = {
		.protocol = 2,
		.report_desc = {
			.desc = mouse_rel_report_desc,
			.len = sizeof(mouse_rel_report_desc),
		},
		.report_length = 6,
		.subclass = 1,
	};

    // struct usbg_f_hid_attrs f_attrs_keyboard = {
	// 	.protocol = 1,
	// 	.report_desc = {
	// 		.desc = keyboard_report_desc,
	// 		.len = sizeof(keyboard_report_desc),
	// 	},
	// 	.report_length = 8,
	// 	.subclass = 1,
	// };

	// 初始化资源时，需要mount -t configfs none /sys/kernel/config
	usbg_ret = (usbg_error)usbg_init("/sys/kernel/config", &s);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error on usbg init\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		return false;
	}

	// 检查并删除现有的 gadget
    usbg_gadget *existing_gadget = usbg_get_gadget(s, "g1");
    if (existing_gadget) {
		printf("rm exist gadget\n");
        usbg_rm_gadget(existing_gadget, USBG_RM_RECURSE);
    }

	usbg_ret = (usbg_error)usbg_create_gadget(s, "g1", &g_attrs, &g_strs, &g);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating gadget\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_cleanup(s);
		return false;
	}

    // 鼠标
	usbg_ret = (usbg_error)usbg_create_function(g, USBG_F_HID, "usb0", &f_attrs_mouse, &f_hid_mouse);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating mouse function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
	}

    // // 键盘
    // usbg_ret = (usbg_error)usbg_create_function(g, USBG_F_HID, "usb1", &f_attrs_keyboard, &f_hid_keyboard);
	// if (usbg_ret != USBG_SUCCESS) {
	// 	fprintf(stderr, "Error creating keyboard function\n");
	// 	fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
	// 			usbg_strerror(usbg_ret));
	// 	usbg_rm_gadget(g, USBG_RM_RECURSE);
	// 	usbg_cleanup(s);
	// 	return false;
	// }

	usbg_ret = (usbg_error)usbg_create_config(g, 1, "The only one", NULL, &c_strs, &c);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating config\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
	}

	usbg_ret = (usbg_error)usbg_add_config_function(c, "usb0", f_hid_mouse);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error adding mouse function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
	}

    // usbg_ret = (usbg_error)usbg_add_config_function(c, "usb1", f_hid_keyboard);
	// if (usbg_ret != USBG_SUCCESS) {
	// 	fprintf(stderr, "Error adding keyboard function\n");
	// 	fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
	// 			usbg_strerror(usbg_ret));
	// 	usbg_rm_gadget(g, USBG_RM_RECURSE);
	// 	usbg_cleanup(s);
	// 	return false;
	// }

	// // 获取可用的UDC名称
	// // char* udc_name = get_available_udc();
    usbg_udc* udc = usbg_get_udc(s, "fc400000.usb");
    if (udc == NULL) {
        fprintf(stderr, "No available UDC found\n");
        usbg_rm_gadget(g, USBG_RM_RECURSE);
        usbg_cleanup(s);
        return false;
    }

	printf("udc name is : %s\n", usbg_get_udc_name(udc));

	usbg_ret = (usbg_error)usbg_enable_gadget(g, udc);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error enabling gadget\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
	}

	printf("---------------usbg create success---------------\n");
	return true;
}

// 相对--绝对坐标互转修改配置
bool SetHIDFuncAttr(char *mouse_report_desc, unsigned int size, int report_len)
{
    // printf("size is : %d\n", size);
    if (!mouse_fd) {
        printf("write fd is null\n");
        usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
        return false;
    }
    close(mouse_fd);

    if (!keyboard_fd) {
        printf("write fd is null\n");
        usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
        return false;
    }
    close(keyboard_fd);

    usbg_config *c;
    usbg_function *new_f_mouse_hid;
    usbg_function *new_f_keyboard_hid;
    usbg_error usbg_ret = (usbg_error)usbg_disable_gadget(g);
	if (usbg_ret != USBG_SUCCESS) {
        fprintf(stderr, "Error unbinding UDC: %s\n", usbg_error_name(usbg_ret));
        if (g)
		    usbg_rm_gadget(g, USBG_RM_RECURSE);
        usbg_cleanup(s);
		return false;
	}

    struct usbg_config_strs new_c_strs = {
		.configuration = "1xHID"
	};

    struct usbg_f_hid_attrs new_f_mouse_attrs = {
		.protocol = 1,
		.report_desc = {
			.desc = mouse_report_desc,
			.len = size,
		},
		.report_length = report_len,
		.subclass = 1,
	};

    struct usbg_f_hid_attrs new_f_keyboard_attrs = {
		.protocol = 1,
		.report_desc = {
			.desc = keyboard_report_desc,
			.len = sizeof(keyboard_report_desc),
		},
		.report_length = 8,
		.subclass = 1,
	};

    usbg_function *f_old_mouse_hid = usbg_get_function(g, USBG_F_HID, "usb0");
    if (f_old_mouse_hid == NULL) {
        printf("get func error\n");
        usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
    }

    usbg_function *f_old_keyboard_hid = usbg_get_function(g, USBG_F_HID, "usb1");
    if (f_old_keyboard_hid == NULL) {
        printf("get func error\n");
        usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
    }

    // printf("111111111\n");
    usbg_ret = (usbg_error)usbg_rm_function(f_old_mouse_hid, USBG_RM_RECURSE);
    if (usbg_ret != USBG_SUCCESS) {
        fprintf(stderr, "Error removing function: %s\n", usbg_error_name(usbg_ret));
        usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
        return false;
    }

    usbg_ret = (usbg_error)usbg_rm_function(f_old_keyboard_hid, USBG_RM_RECURSE);
    if (usbg_ret != USBG_SUCCESS) {
        fprintf(stderr, "Error removing function: %s\n", usbg_error_name(usbg_ret));
        usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
        return false;
    }
    // printf("2222222222222\n");

    // Remove configuration
    struct usbg_config *cfg;
    cfg = usbg_get_config(g, 1, "The only one");
    if (cfg == NULL) {
        printf("get config error\n");
        usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
        return false;
    }
    // printf("333333\n");

    usbg_ret = (usbg_error)usbg_rm_config(cfg, USBG_RM_RECURSE);
    if (usbg_ret != USBG_SUCCESS) {
        fprintf(stderr, "Error removing config: %s\n", usbg_error_name(usbg_ret));
        usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
        return false;
    }

    // printf("4444444\n");

    usbg_ret = (usbg_error)usbg_create_function(g, USBG_F_HID, "usb0", &new_f_mouse_attrs, &new_f_mouse_hid);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
	}

    usbg_ret = (usbg_error)usbg_create_function(g, USBG_F_HID, "usb1", &new_f_keyboard_attrs, &new_f_keyboard_hid);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
	}

    usbg_ret = (usbg_error)usbg_create_config(g, 1, "The only one", NULL, &new_c_strs, &c);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating config\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
	}

	usbg_ret = (usbg_error)usbg_add_config_function(c, "usb0", new_f_mouse_hid);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error adding function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
	}

    usbg_ret = (usbg_error)usbg_add_config_function(c, "usb1", new_f_keyboard_hid);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error adding function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
	}

    usbg_udc* udc = usbg_get_udc(s, "fc400000.usb");
    if (udc == NULL) {
        fprintf(stderr, "No available UDC found\n");
        usbg_rm_gadget(g, USBG_RM_RECURSE);
        usbg_cleanup(s);
        return false;
    }

    usbg_ret = (usbg_error)usbg_enable_gadget(g, udc);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error enabling gadget\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
		usbg_cleanup(s);
		return false;
	}

    mouse_fd = open("/dev/hidg0", O_RDWR);
	if (mouse_fd < 0) {
        perror("Failed to open /dev/hidg0");
        if (g)
		    usbg_rm_gadget(g, USBG_RM_RECURSE);
        usbg_cleanup(s);
        return EXIT_FAILURE;
    }

    keyboard_fd = open("/dev/hidg1", O_RDWR | O_NONBLOCK);
	if (keyboard_fd < 0) {
        perror("Failed to open /dev/hidg1");
        if (g)
		    usbg_rm_gadget(g, USBG_RM_RECURSE);
        usbg_cleanup(s);
        return EXIT_FAILURE;
    }

	printf("---------------usbg reset success---------------\n");
	return true;
}

void HIDWrite(int fd, unsigned char* hid_data, size_t len)
{
    // 传输数据到主机
    ssize_t bytes_written = write(fd, hid_data, len);
    if (bytes_written < 0) {
        perror("Failed to write data to /dev/hidg0");
    } else {
        // // 打印键盘数据
        // printf("键盘数据: ");
        // for (int i = 0; i < len; i++) {
        //     printf("%02X ", hid_data[i]);
        // }
        // printf("\n");
        // if (len >= 3) {
        //     unsigned char modifier = hid_data[0];
        //     unsigned char keycode = hid_data[2];

        //     // 打印按键代码
        //     if (hid_data[0] != 0) {
        //         printf("修饰键: %02X, 按键代码: %02X, 键: %s\n", modifier, keycode, get_modifier_key_name(modifier));
        //     } else if (hid_data[2] != 0) {
        //         printf("修饰键: %02X, 按键代码: %02X, 键: %s\n", modifier, keycode, get_key_name(keycode));
        //     }
        // }
        // printf("\n----------------键盘测试成功-------------------\n");
        // printf("\n");

        // 打印鼠标数据
        printf("鼠标数据: ");
        for (int i = 0; i < len; i++) {
            printf("%02X ", hid_data[i]);
        }
        printf("\n");
        printf("\n----------------鼠标测试成功-------------------\n");
        printf("\n");
    }
}

void SetNewMaxAttr(int16_t value)
{
    unsigned char low_char = value & 0xFF;
    unsigned char high_char = (value & 0xFF00) >> 8;
    printf("low is : 0x%02x, high is : 0x%02x\n", low_char, high_char);
    if (high_char == 0x00) {
        printf("high_char is null ,dont support this value : %d\n", value);
        return;
    }
    mouse_abs_report_desc[41] = low_char;
    mouse_abs_report_desc[42] = high_char;
    mouse_abs_report_desc[46] = low_char;
    mouse_abs_report_desc[47] = high_char;
}

// bool stop = false;
// int pipe_fd[2];

int main(void) {
	signal(SIGPIPE, SIG_IGN);
    signal(SIGKILL, ProgExit);
    signal(SIGINT, ProgExit);
    signal(SIGTERM, ProgExit);
    signal(SIGSEGV, errorCb);
    signal(SIGABRT, errorCb);

    //创建hid设备（从设备）
	if(!CreateHIDDevice()) {
		printf("create HID device fail\n");
		return EXIT_FAILURE;
	}

	mouse_fd = open("/dev/hidg0", O_RDWR);
	if (mouse_fd < 0) {
        perror("Failed to open /dev/hidg0");
        if (g)
		    usbg_rm_gadget(g, USBG_RM_RECURSE);
        usbg_cleanup(s);
        return EXIT_FAILURE;
    }

    // keyboard_fd = open("/dev/hidg1", O_RDWR | O_NONBLOCK);
	// if (keyboard_fd < 0) {
    //     perror("Failed to open /dev/hidg1");
    //     if (mouse_fd)
    //         close(mouse_fd);
    //     if (g)
	// 	    usbg_rm_gadget(g, USBG_RM_RECURSE);
    //     usbg_cleanup(s);
    //     return EXIT_FAILURE;
    // }

    // if (pipe(pipe_fd) == -1) {
    //     perror("pipe");
    //     close(keyboard_fd);
    //     return 1;
    // }

	printf("------------------success open /dev/hidg1(keyboard) and /dev/high0(mouse), start write loop------------------\n");

    // while(1) {
    //     sleep(1);
    //     // SetHIDFuncAttr(mouse_rel_report_desc, sizeof(mouse_rel_report_desc), 4);
    //     // printf("finish abs 2 rel\n");
    //     // SetHIDFuncAttr(mouse_abs_report_desc, sizeof(mouse_abs_report_desc), 6);
    //     // printf("finish rel 2 abs\n");
    // }
    
    // 读取键盘灯状态改变报文
    // std::thread thread = std::thread([&](){
    //     unsigned char buf[8];
    //     while (1) {
    //         ssize_t res = read(keyboard_fd, buf, sizeof(buf));
    //         if (res > 0) {
    //             printf("Read %ld bytes from HID device: ", res);
    //             for (ssize_t i = 0; i < res; i++) {
    //                 printf("%02x ", buf[i]);
    //             }
    //             printf("\n");
    //         } else if (res < 0) {
    //             if (errno == EAGAIN || errno == EWOULDBLOCK) {
    //                 // printf("no data\n");
    //             } else {
    //                 perror("Error reading from HID device");
    //                 break;
    //             }
    //         } else {
    //             printf("other error\n");
    //             break;
    //         }
    //     }
    //     printf("exit thread\n");
    // });

    // sleep(1);

    // SetHIDFuncAttr(mouse_rel_report_desc, sizeof(mouse_rel_report_desc), 4);

    // sleep(1);

    // sleep(10);
    // stop = true;
    // write(pipe_fd[1], "x", 1);
    // thread.join();

    // close(pipe_fd[0]);
    // close(pipe_fd[1]);
    // printf("close\n");

    // sleep(30);

    // usleep(500000);
    // unsigned char keyboard_test_report[8] = {0};
    // printf("sending first keyboard report\n");
    // HIDWrite(keyboard_fd, keyboard_test_report, sizeof(keyboard_test_report));
    // for (int i = 0; i < 50; i++) {
    //     usleep(10000);
    //     keyboard_test_report[2] = 0x04; // 键入a
    //     HIDWrite(keyboard_fd, keyboard_test_report, sizeof(keyboard_test_report));
    //     keyboard_test_report[2] = 0x00;
    //     HIDWrite(keyboard_fd, keyboard_test_report, sizeof(keyboard_test_report));
    // }

    // test_report[2] = 0x39; // 键入左边capslock
    // HIDWrite(test_report, sizeof(test_report));
    // test_report[2] = 0x00;
    // HIDWrite(test_report, sizeof(test_report));

    // for (int i = 0; i < 50; i++) {
    //     usleep(10000);
    //     test_report[2] = 0x04; // 键入a
    //     HIDWrite(test_report, sizeof(test_report));
    //     test_report[2] = 0x00;
    //     HIDWrite(test_report, sizeof(test_report));
    // }

    // test_report[2] = 0x39; // 键入左边capslock
    // HIDWrite(test_report, sizeof(test_report));
    // test_report[2] = 0x00;
    // HIDWrite(test_report, sizeof(test_report));

    
    sleep(1);
    unsigned char mouse_rel_test_report[6] = {0};
    printf("sending first mouse report\n");
    HIDWrite(mouse_fd, mouse_rel_test_report, sizeof(mouse_rel_test_report));
    // // 鼠标绝对移动
    // unsigned char mouse_abs_test_report[6] = {0};
    // for (int i = 0; i < 20; i++) {
    //     usleep(100000);
    //     mouse_abs_test_report[1] = 0X00;
    //     mouse_abs_test_report[2] = 0X02;
    //     mouse_abs_test_report[3] = 0X00;
    //     mouse_abs_test_report[4] = 0X02;
    //     HIDWrite(mouse_fd, mouse_abs_test_report, sizeof(mouse_abs_test_report));
    //     usleep(100000);
    //     mouse_abs_test_report[1] = 0X00;
    //     mouse_abs_test_report[2] = 0X03;
    //     mouse_abs_test_report[3] = 0X00;
    //     mouse_abs_test_report[4] = 0X03;
    //     HIDWrite(mouse_fd, mouse_abs_test_report, sizeof(mouse_abs_test_report));
    // }

    // SetNewMaxAttr(4096);
    
    // if (!SetHIDFuncAttr(mouse_abs_report_desc, sizeof(mouse_abs_report_desc), 6)) {
    //     printf("reset func attr error\n");
    //     return EXIT_FAILURE;
    // }

    // // Print updated descriptor
    // for (size_t i = 0; i < sizeof(mouse_abs_report_desc); ++i) {
    //     if (i % 16 == 0)
    //         printf("\n");
    //     printf("%02X ", mouse_abs_report_desc[i]);
    // }

    // sleep(1);
    // for (int i = 0; i < 20; i++) {
    //     usleep(100000);
    //     mouse_abs_test_report[1] = 0X00;
    //     mouse_abs_test_report[2] = 0X06;
    //     mouse_abs_test_report[3] = 0X00;
    //     mouse_abs_test_report[4] = 0X06;
    //     HIDWrite(mouse_fd, mouse_abs_test_report, sizeof(mouse_abs_test_report));
    //     usleep(100000);
    //     mouse_abs_test_report[1] = 0X00;
    //     mouse_abs_test_report[2] = 0X04;
    //     mouse_abs_test_report[3] = 0X00;
    //     mouse_abs_test_report[4] = 0X04;
    //     HIDWrite(mouse_fd, mouse_abs_test_report, sizeof(mouse_abs_test_report));
    // }

    // if (!SetHIDFuncAttr(mouse_rel_report_desc, sizeof(mouse_rel_report_desc), 4)) {
    //     printf("reset func attr error\n");
    //     return EXIT_FAILURE;
    // }

    // sleep(1);
    // // 鼠标相对坐标移动
    // for (int j = 0; j < 20; j++) {
    //     usleep(100000);
    //     mouse_rel_test_report[2] = 0X7F;
    //     HIDWrite(mouse_fd, mouse_rel_test_report, sizeof(mouse_rel_test_report));
    //     usleep(100000);
    //     mouse_rel_test_report[2] = 0X90;
    //     HIDWrite(mouse_fd, mouse_rel_test_report, sizeof(mouse_rel_test_report));
    // }

    // 鼠标按键
    for (int i = 0; i < 100; i++) {
        usleep(100000);
        mouse_rel_test_report[0] = 0x02;
        HIDWrite(mouse_fd, mouse_rel_test_report, sizeof(mouse_rel_test_report));
        mouse_rel_test_report[0] = 0x00;
        HIDWrite(mouse_fd, mouse_rel_test_report, sizeof(mouse_rel_test_report));
        usleep(100000);
        // sleep(1);
    }

    // 鼠标滚轮
    // for (int i = 0; i < 100; i++) {
    //     usleep(100000);
    //     test_report[3] = 0xFF;
    //     HIDWrite(test_report, sizeof(test_report));
    //     usleep(100000);
    //     test_report[3] = 0x01;
    //     HIDWrite(test_report, sizeof(test_report));
    //     // sleep(1);
    // }

    if (mouse_fd)
        close(mouse_fd);
    if (keyboard_fd)
        close(keyboard_fd);
	usbg_rm_gadget(g, USBG_RM_RECURSE);
	usbg_cleanup(s);
    return EXIT_SUCCESS;
}