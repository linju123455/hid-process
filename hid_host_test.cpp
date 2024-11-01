#include <hidapi/hidapi.h>
#include <libusb-1.0/libusb.h>
#include "iostream"
#include "signal.h"
#include "unistd.h"
#include "cstring"
#include <fcntl.h>
#include "report_item.h"
#include <chrono>
#include <thread>
#include <future>
#include <vector>
#include <mutex>
int                         m_numlock = 1;
    int                         m_capslock = 0;
    int                         m_scrollock = 0;
hid_device* mouse_handle = nullptr; // 有可能出现n个鼠标或者n个键盘，后续多相同类型的设备返回句柄给上层管理
hid_device* keyboard_handle = nullptr;
// bool hasReportId = false;
static int time_count = 0; // 调用次数计数器
static auto start = std::chrono::steady_clock::now(); // 开始时间
std::mutex m_mutex;

// // 键盘灯，后续一个键盘设备维护一个
// int numlock = 0;
// int capslock = 0;
// int scrollock = 0;

MouseReportParseInfo parse_info; // 鼠标解析信息

typedef enum {
	ev_Undefined = -1,
	ev_Keyboard,
	ev_Mouse,
	ev_Light,
	ev_Light_Request,
} EventType;

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
		case 0x47: return "srollLock";
		case 0x53: return "numLock";
        // 添加更多按键代码和对应字符
        default: return "未知按键";
    }
}

const char *hid_bus_name(hid_bus_type bus_type) {
	static const char *const HidBusTypeName[] = {
		"Unknown",
		"USB",
		"Bluetooth",
		"I2C",
		"SPI",
	};

	if ((int)bus_type < 0)
		bus_type = HID_API_BUS_UNKNOWN;
	if ((int)bus_type >= (int)(sizeof(HidBusTypeName) / sizeof(HidBusTypeName[0])))
		bus_type = HID_API_BUS_UNKNOWN;

	return HidBusTypeName[bus_type];
}

int32_t extractBits(const unsigned char* data, int startBit, int length) {
    int32_t result = 0;
    for (int i = 0; i < length; ++i) {
        int byteIndex = (startBit + i) / 8; // 需读取的字节下标
        int bitIndex = (startBit + i) % 8; // 需读取的位下标
        result |= ((data[byteIndex] >> bitIndex) & 1) << i; // 按位读取相应的值
    }
    return result;
}

void parseMouseReport(const unsigned char* data, int32_t *x, int32_t *y, int32_t *wheel, int32_t *button) {
    // 假设 X, Y, Wheel 和 Button 在位级别上的位置和长度
    *x = extractBits(data, parse_info.xBitIndex, parse_info.xbitCount);   // X坐标
	if (*x & (1 << (parse_info.xbitCount - 1))) { // 如果最高位为1，减去它本身的2倍数
		*x -= (1 << parse_info.xbitCount);
		// printf("x is : %d\n", *x);
		// *x = static_cast<int>(*x * (127.0 / parse_info.xyLogicalMax));
		// if (*x == 0)
		// 	*x = -1;
	} else {
		// printf("x is : %d\n", *x);
		// *x = static_cast<int>(*x * (127.0 / parse_info.xyLogicalMax));
		// if (*x == 0)
		// 	*x = 1;
	}
	
    *y = extractBits(data, parse_info.yBitIndex, parse_info.ybitCount);   // Y坐标
	if (*y & (1 << (parse_info.ybitCount - 1))) { // 如果最高位为1，减去它本身的2倍数
		*y -= (1 << parse_info.ybitCount);
		// printf("y is : %d\n", *y);
		// *y = static_cast<int>(*y * (127.0 / parse_info.xyLogicalMax));
		// if (*y== 0)
		// 	*y = -1;
	} else {
		// printf("y is : %d\n", *y);
		// *y = static_cast<int>(*y * (127.0 / parse_info.xyLogicalMax));
		// if (*y == 0)
		// 	*y = 1;
	}

    *wheel = extractBits(data, parse_info.wheelBitIndex, parse_info.wheelbitCount);  // Wheel
	if (*wheel & (1 << (parse_info.wheelbitCount - 1)))  // 如果最高位为1，减去它本身的2倍数
		*wheel -= (1 << parse_info.wheelbitCount);
    *button = extractBits(data, parse_info.buttonsBitIndex, parse_info.buttonsbitCount); // Button
}

static void print_device(struct hid_device_info *cur_dev) {
	printf("Device Found\n  type: %04hx %04hx\n  path: %s\n  serial_number: %ls", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path, cur_dev->serial_number);
	printf("\n");
	printf("  Manufacturer: %ls\n", cur_dev->manufacturer_string);
	printf("  Product:      %ls\n", cur_dev->product_string);
	printf("  Release:      %hx\n", cur_dev->release_number);
	printf("  Interface:    %d\n",  cur_dev->interface_number);
	printf("  Usage (page): 0x%hx (0x%hx)\n", cur_dev->usage, cur_dev->usage_page);
	printf("  Bus type: %u (%s)\n", (unsigned)cur_dev->bus_type, hid_bus_name(cur_dev->bus_type));
	printf("\n");
}

static void print_hid_report_descriptor_from_device(hid_device *device) {
	unsigned char descriptor[HID_API_MAX_REPORT_DESCRIPTOR_SIZE];
	int res = 0;

	printf("  Report Descriptor: ");
	res = hid_get_report_descriptor(device, descriptor, sizeof(descriptor));
	if (res < 0) {
		printf("error getting: %ls", hid_error(device));
	}
	else {
		printf("(%d bytes)", res);
	}

	for (int i = 0; i < res; i++) {
		// if (descriptor[i] == 0x85) { // 检查是否包含 Report ID
        //     hasReportId = true;
        // }
		if (i % 10 == 0) {
			printf("\n");
		}
		printf("0x%02x, ", descriptor[i]);
	}
	printf("\n");

	ri_Parse(descriptor, res);
	printf("ri Parse success\n");
	
	get_parse_result(&parse_info);
	printf("buttonsBitIndex is : %d, buttonsbitCount is : %d \
			xBitIndex is : %d, xbitCount is : %d \
			yBitIndex is : %d, ybitCount is : %d \
			wheelBitIndex is : %d, wheelbitCount is : %d \
			xyLogicalmin is : %d, xyLogicalmax is : %d \
			mouseTotalBitIndex is : %d\n",
			parse_info.buttonsBitIndex, parse_info.buttonsbitCount, 
			parse_info.xBitIndex, parse_info.xbitCount, 
			parse_info.yBitIndex, parse_info.ybitCount, 
			parse_info.wheelBitIndex, parse_info.wheelbitCount,
			parse_info.xyLogicalMin, parse_info.xyLogicalMax,
			parse_info.mouseTotalBitIndex);
	if ((parse_info.mouseTotalBitIndex / 8) % 2) {
		descriptor[res - 2] = 0x95;
		descriptor[res - 1] = 0x01;
		descriptor[res] = 0x81;
		descriptor[res + 1] = 0x01;
		descriptor[res + 2] = 0xc0;
		descriptor[res + 3] = 0xc0;
		for (int i = 0; i < res + 4; i++) {
			if (i % 10 == 0) {
				printf("\n");
			}
			printf("0x%02x, ", descriptor[i]);
		}
		printf("\n");
	}
}

static void print_hid_report_descriptor_from_path(const char *path) {
	hid_device *device = hid_open_path(path);
	if (device) {
		print_hid_report_descriptor_from_device(device);
		hid_close(device);
	}
	else {
		printf("  Report Descriptor: Unable to open device by path\n");
	}
}

static void print_devices_with_descriptor(struct hid_device_info *cur_dev) {
	for (; cur_dev; cur_dev = cur_dev->next) {
		print_device(cur_dev);
		print_hid_report_descriptor_from_path(cur_dev->path);
	}
}

static int write_sys_led_status(const char* cmd, int led_status_code)
{
	FILE *fp;
	fp = fopen(cmd, "w");
	if (fp == NULL) {
		fprintf(stderr, "Failed to open %s\n", cmd);
		return -1;
	} else {
		fprintf(fp, "%d\n", led_status_code);
		fclose(fp);
	}
	return 0;
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

void ProgExit(int signo)
{
    printf("come in prog exit\n");
	// 关闭设备
	if (mouse_handle)
		hid_close(mouse_handle);

	if (keyboard_handle)
		hid_close(keyboard_handle);
 
	// 清理hidapi
	hid_exit();
    exit(0);
}

void errorCb(int signo)
{
	printf("come in errorCb\n");
	// 关闭设备
	if (mouse_handle)
		hid_close(mouse_handle);

	if (keyboard_handle)
		hid_close(keyboard_handle);
 
	// 清理hidapi
	hid_exit();
    exit(-1);
}

enum DeviceType {
	USB_UNKOWN = -1,
	USB_KEBOARD = 0,
	USB_MOUTH = 1
};

bool GetUSBDeviceInfo(DeviceType& type, int* vendor_id, int* product_id)
{
	*vendor_id = -1;
	*product_id = -1;
	type = DeviceType::USB_UNKOWN;
    libusb_device **devs;                         //devices
	libusb_context *ctx = NULL;

    //初始化libusb
    int ret = libusb_init(&ctx);
    if (ret < 0) {
		fprintf(stderr, "libusb Init error: %s\n", libusb_strerror((libusb_error)ret));
		return false;
	}

    //获取USB设备列表
    int cnt = libusb_get_device_list(ctx, &devs);
	if (cnt < 0) {
		fprintf(stderr, "Error retrieving device list: %s\n", libusb_strerror((libusb_error)cnt));
		return false;
	}

    //获取设备描述
    for (ssize_t i = 0; i < cnt; i++) {
		struct libusb_device_descriptor desc;
		ret = libusb_get_device_descriptor(devs[i], &desc);
		if (ret < 0) {
			fprintf(stderr, "无法获取设备描述符: %s\n", libusb_error_name(ret));
			continue;
		}

		//打印设备信息
		printf("-----------------device info start------------------\n");
		printf("%04x:%04x (bus %d, device %d)\n",  
            	desc.idVendor, desc.idProduct,  
            	libusb_get_bus_number(devs[i]), libusb_get_device_address(devs[i]));
		printf("-----------------device info end------------------\n");

		// 获取设备的配置描述符
        struct libusb_config_descriptor *config;
        ret = libusb_get_config_descriptor(devs[i], 0, &config);
        if (ret < 0) {
            fprintf(stderr, "无法获取配置描述符: %s\n", libusb_error_name(ret));
            continue;
        }

		// 遍历接口描述符，检查是否为键盘设备
        for (int j = 0; j < config->bNumInterfaces; j++) {
            const struct libusb_interface *iface = &config->interface[j];
            for (int k = 0; k < iface->num_altsetting; k++) {
                const struct libusb_interface_descriptor *altsetting = &iface->altsetting[k];
                if (altsetting->bInterfaceClass == 0x03 && 
					altsetting->bInterfaceSubClass == 0x01 && 
					altsetting->bInterfaceProtocol == 0x01) {
                    printf("---->设备 VID: 0x%04X, PID: 0x%04X, 接口：%d 是键盘设备\n", desc.idVendor, desc.idProduct, altsetting->bInterfaceNumber);
					type = DeviceType::USB_KEBOARD;
					*vendor_id = desc.idVendor;
					*product_id = desc.idProduct;
                } else if (altsetting->bInterfaceClass == 0x03 && 
							altsetting->bInterfaceSubClass == 0x01 && 
							altsetting->bInterfaceProtocol == 0x02) {
					printf("---->设备 VID: 0x%04X, PID: 0x%04X, 接口：%d 是鼠标设备\n", desc.idVendor, desc.idProduct, altsetting->bInterfaceNumber);
					type = DeviceType::USB_MOUTH;
					*vendor_id = desc.idVendor;
					*product_id = desc.idProduct;
				} else {
					// printf("---->error device: 0x%04X, PID: 0x%04X \n", desc.idVendor, desc.idProduct);
				}
            }
        }

        // 释放配置描述符
        libusb_free_config_descriptor(config);
	}

	// 释放设备列表
	libusb_free_device_list(devs, 1);

	// 关闭 libusb
	libusb_exit(ctx);

	return true;
}

bool HIDWrite(const unsigned char led_status_code)
{
	// int brightness = -1;
	// int res = read_sys_led_status("cat /sys/class/leds/input12\:\:numlock/brightness", &brightness);
	// if (res < 0) {
	// 	return false;
	// }
	// printf("first Num Lock LED brightness: %d\n", brightness);
	unsigned char report[2];
	report[0] = 0x00;
	report[1] = led_status_code;
	int res = hid_write(keyboard_handle, report, sizeof(report));
	if (res < 0) {
		printf("hid write error\n");
		return false;
	}

	// res = read_sys_led_status("cat /sys/class/leds/input12\:\:numlock/brightness", &brightness);
	// if (res < 0) {
	// 	return false;
	// }
	// printf("second Num Lock LED brightness: %d\n", brightness);
	return true;
}

void HIDRead(EventType type)
{
	if (type == EventType::ev_Mouse) {
		printf("----------------请移动鼠标-------------------\n");
		// 读取鼠标数据
		while (1) {
			// // 每次函数调用时，检查是否已经过去1秒
			// if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() >= 1) {
			//     std::cout << "SendKvmEvent was called " << time_count << " times in last second." << std::endl;
			//     time_count = 0; // 重置计数器
			//     start = std::chrono::steady_clock::now(); // 重置开始时间
			// }

			// time_count++; // 增加调用次数

			unsigned char data[64];
			// unsigned char *buffer = data + 1;
			int res = hid_read(mouse_handle, data, sizeof(data));
			if (res < 0) {
				printf("读取鼠标数据失败\n");
				break;
			}
	
			// 打印鼠标数据
			printf("鼠标数据: ");
			for (int i = 0; i < res; i++) {
				printf("%02X ", data[i]);
			}

			int32_t x, y, wheel, button;
			parseMouseReport(data, &x, &y, &wheel, &button);
			// if (hasReportId)
			// 	parseMouseReport(buffer, &x, &y, &wheel, &button);
			// else
			// 	parseMouseReport(data, &x, &y, &wheel, &button);
			
			printf("\n");
			printf("X is : %d, Y is :%d, Wheel is : %d, button is : %d\n", x, y, wheel, button);
			printf("\n----------------hidapi测试成功-------------------\n");
			printf("\n");
		}
	} else if (type == EventType::ev_Keyboard) {
		printf("----------------请按键-------------------\n");
		// 读取键盘数据
		while (1) {
			std::lock_guard<std::mutex> lk(m_mutex);
			unsigned char data[64];
			int res = hid_read(keyboard_handle, data, sizeof(data));
			if (res < 0) {
				printf("读取键盘数据失败\n");
				break;
			}
	
			// 打印键盘数据
			printf("键盘数据: ");
			for (int i = 0; i < res; i++) {
				printf("%02X ", data[i]);
			}
			printf("\n");
			if (res >= 3) {
				unsigned char modifier = data[0];
				unsigned char keycode = data[2];

				// 打印按键代码
				if (data[0] != 0) {
					printf("修饰键: %02X, 按键代码: %02X, 键: %s\n", modifier, keycode, get_modifier_key_name(modifier));
				} else if (data[2] != 0) {
					printf("修饰键: %02X, 按键代码: %02X, 键: %s\n", modifier, keycode, get_key_name(keycode));
				}

				if (data[2] == 0x53) {
					m_numlock = m_numlock == 1 ? 0 : 1;
					unsigned char lightValue = m_numlock | m_capslock << 1 | m_scrollock << 2;
					HIDWrite(lightValue);
					printf("ledMsg.msg.lightValue1 is : %d\n", lightValue);
				} else if (data[2] == 0x39) {
					m_capslock = m_capslock == 0 ? 1 : 0;
					unsigned char lightValue = m_numlock | m_capslock << 1 | m_scrollock << 2;
					HIDWrite(lightValue);
					printf("ledMsg.msg.lightValue2 is : %d\n", lightValue);
				} else if (data[2] == 0x47) {
					m_scrollock = m_scrollock == 0 ? 1 : 0;
					unsigned char lightValue = m_numlock | m_capslock << 1 | m_scrollock << 2;
					HIDWrite(lightValue);
					printf("ledMsg.msg.lightValue3 is : %d\n", lightValue);
				}
			}

			printf("\n----------------hidapi测试成功-------------------\n");
			printf("\n");
		}
	}
}

void DeviceDiscover(std::vector<std::future<void>>& futures)
{
	hid_device_info *device_info = hid_enumerate(0, 0);
	for (; device_info; device_info = device_info->next) {
		if (device_info->usage_page != 0x01) {
			continue;
		}

		// mouse
		if (device_info->usage == 0x02) {
			mouse_handle = hid_open_path(device_info->path);
			if (!mouse_handle) {
				fprintf(stderr, "Unable to open mouse device, %ls\n", hid_error(mouse_handle));
				hid_exit();
				return;
			}

			print_device(device_info);
			print_hid_report_descriptor_from_device(mouse_handle);

			futures.push_back(std::async(std::launch::async, [](){
				HIDRead(EventType::ev_Mouse);
			}));
			printf("find mouse\n");
		}

		// keyboard
		if (device_info->usage == 0x06) {
			keyboard_handle = hid_open_path(device_info->path);
			if (!keyboard_handle) {
				fprintf(stderr, "Unable to open keyboard device, %ls\n", hid_error(keyboard_handle));
				hid_exit();
				return;
			}

			unsigned char descriptor[HID_API_MAX_REPORT_DESCRIPTOR_SIZE];
			int res = 0;

			printf(" keyboard Report Descriptor: ");
			res = hid_get_report_descriptor(keyboard_handle, descriptor, sizeof(descriptor));
			if (res < 0) {
				printf("error getting: %ls", hid_error(keyboard_handle));
			}
			else {
				printf("(%d bytes)", res);
			}
			print_device(device_info);
			for (int i = 0; i < res; i++) {
				if (i % 10 == 0) {
					printf("\n");
				}
				printf("0x%02x, ", descriptor[i]);
			}
			printf("\n");

			futures.push_back(std::async(std::launch::async, [](){
				HIDRead(EventType::ev_Keyboard);
			}));
			printf("find keyboard\n");
			HIDWrite(0x01);
		}
	}
}

int main(void) {
	signal(SIGPIPE, SIG_IGN);
    signal(SIGKILL, ProgExit);
    signal(SIGINT, ProgExit);
    signal(SIGTERM, ProgExit);
    signal(SIGSEGV, errorCb);
    signal(SIGABRT, errorCb);


	// int vid = 0;
	// int pid = 0;
	// DeviceType type;
	// // 获取USB设备的VendorID以及ProductID以及设备类型
	// if (!GetUSBDeviceInfo(type, &vid, &pid)) 
	// 	return EXIT_FAILURE;

	// if (vid == -1 || pid == -1 || type == DeviceType::USB_UNKOWN) {
	// 	printf("device is not HID\n");
	// 	return EXIT_FAILURE;
	// }
	
	// printf("vid:%04X,pid:%04X\n",vid,pid);

	// //初始化HIDAPI
    // if (hid_init())
    //     return EXIT_FAILURE;

	// //打开设备
	// keyboard_handle = hid_open(vid,pid,NULL);
    // if (!keyboard_handle) {
	// 	fprintf(stderr, "Unable to open device, %ls\n", hid_error(keyboard_handle));
	// 	hid_exit();
	// 	return EXIT_FAILURE;
	// }

	// // //循环读取hid设备的信息
	// HIDRead(ev_Keyboard);

	std::vector<std::future<void>> futures;
	DeviceDiscover(futures);

	sleep(3);

	// //设置键盘led状态
	// if (!HIDWrite(0x01)) {
	// 	hid_close(keyboard_handle);
	// 	hid_exit();
	// 	return EXIT_FAILURE;
	// }
	// numlock = 1;
	// printf("set numlock success\n");
	// sleep(3);
	// if (!HIDWrite(0x03)) {
	// 	hid_close(keyboard_handle);
	// 	hid_exit();
	// 	return EXIT_FAILURE;
	// }
	// capslock = 1;
	// printf("set capslock success\n");
	// sleep(3);
	// if (!HIDWrite(0x02)) {
	// 	hid_close(keyboard_handle);
	// 	hid_exit();
	// 	return EXIT_FAILURE;
	// }
	// numlock = 0;
	// printf("unset numlock success\n");

	while (1) {
       	std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
	// hid_device_info *device_info = hid_enumerate(0, 0);
	// print_devices_with_descriptor(device_info);
	// hid_free_enumeration(device_info);

	// handle = hid_open_path("/dev/hidraw0");
	// if (!handle) {
	// 	fprintf(stderr, "Unable to open device, %ls\n", hid_error(handle));
	// 	hid_exit();
	// 	return EXIT_FAILURE;
	// }

	// print_hid_report_descriptor_from_device(handle);

	// printf("------------------success open hid, start read loop------------------\n");

	// //循环读取hid设备的信息
	// HIDRead(type);
 
	// // 关闭设备
	// hid_close(handle);
 
	// // 清理hidapi
	// hid_exit();

	return EXIT_SUCCESS;
}