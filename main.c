/*
Copyright 2015 Moritz Hoffmann <antiguru+deb@gmail.com>

This software is provided under the license specified in LICENSE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libusb-1.0/libusb.h>

#include <getopt.h>

#include "usbcom.h"

#define TIMEOUT 1000

static libusb_context *ctx;

void print_usage() {
    printf("Usage:\n ykush2 [-l]\n");
    printf(" ykush2 [-h hub] {-d|-u} {port|a}\n");
}


enum action {
    YKUSH_NONE, YKUSH_UP, YKUSH_DOWN, YKUSH_LIST
};

/*
 * 
 */
int main(int argc, char** argv) {

    int option;
    int port;
    int hub_id = 1;
    int err;
    enum action action = YKUSH_NONE;
    
    while ((option = getopt(argc, argv, "lh:d:u:")) != -1) {
        switch (option) {
            case 'd': 
                action = YKUSH_DOWN;
                if (!strcmp("a", optarg)) {
                    port = YKUSH_ALL_PORTS;
                } else {
                    port = atoi(optarg);
                }
                break;
            case 'u':
                action = YKUSH_UP;
                if (!strcmp("a", optarg)) {
                    port = YKUSH_ALL_PORTS;
                } else {
                    port = atoi(optarg);
                }
                break;
            case 'l':
                action = YKUSH_LIST;
                break;
            case 'h':
                hub_id = atoi(optarg);
                break;
            default:
                printf("Unknown option %c\n", option);
                print_usage();
                exit(EXIT_FAILURE);
        }
    }
    
    if (!(action == YKUSH_DOWN || action == YKUSH_UP || action == YKUSH_LIST)) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    if (action != YKUSH_LIST && (port != YKUSH_ALL_PORTS && (port < 1 && port > YKUSH_PORTS))) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    err = libusb_init(&ctx);
    if (err < 0) {
        fprintf(stderr, "Failed to initialize USB: %s\n", libusb_error_name(err));
        return err;
    }
    
    libusb_device **list;
    ssize_t size = libusb_get_device_list(ctx, &list);
    libusb_device_handle *hub = NULL;
    int devices = 0;

    for (int i = 0; i < size; i++) {
        struct libusb_device_descriptor desc;
        err = libusb_get_device_descriptor(list[i], &desc);
        if (err < 0) {
            fprintf(stderr, "Failed to enumerate devices: %s\n", libusb_error_name(err));
            goto out_list;
        }
        if (desc.idVendor == YKUSH_USB_VENDOR_ID &&
                desc.idProduct == YKUSH_USB_PRODUCT_ID) {
            devices++;
            if (action == YKUSH_LIST) {
                printf("%i: %04x:%04x ", devices, desc.idVendor, desc.idProduct);
                err = libusb_open(list[i], &hub);
                if (err < 0) {
                    printf("unable to open device");
                } else {
                    unsigned char serial[64];
                    unsigned char product[64];
                    unsigned char manufacturer[64];
                    err = libusb_get_string_descriptor_ascii(hub, desc.iSerialNumber, serial, sizeof(serial));
                    if (err < 0) {
                        libusb_close(hub);
                        goto out_list;
                    }
                    err = libusb_get_string_descriptor_ascii(hub, desc.iProduct, product, sizeof(product));
                    if (err < 0) {
                        libusb_close(hub);
                        goto out_list;
                    }
                    err = libusb_get_string_descriptor_ascii(hub, desc.iManufacturer, manufacturer, sizeof(manufacturer));
                    if (err < 0) {
                        libusb_close(hub);
                        goto out_list;
                    }
                    printf("%s/%s/%s", manufacturer, product, serial);
                    libusb_close(hub);
                    hub = NULL;
                }
                printf("\n");
            } else if (devices == hub_id) {
                err = libusb_open(list[i], &hub);
                if (err < 0) {
                    fprintf(stderr, "Failed to open device: %s\n", libusb_error_name(err));
                    goto out_list;
                }
            }
        }
    }
    
    if (action != YKUSH_LIST) {
        if (hub == NULL) {
            fprintf(stderr, "No hub with ID %i found.\n", hub_id);
            err = EXIT_FAILURE;
            goto out_list;
        }
        
        err = libusb_detach_kernel_driver(hub, 0);
        if (err < 0 && err != LIBUSB_ERROR_NOT_FOUND) {
            fprintf(stderr, "Failed to detach kernel: %s\n", libusb_error_name(err));
            goto out_dev;
        }
        err = libusb_set_configuration(hub, 1);
        if (err < 0) {
            fprintf(stderr, "Failed to set configuration: %s\n", libusb_error_name(err));
            goto out_dev;
        }
    
        err = libusb_claim_interface(hub, 0);
        if (err < 0) {
            fprintf(stderr, "Failed to claim interface: %s\n", libusb_error_name(err));
            goto out_dev;
        }
        
        unsigned char buf[6];
        
        buf[0] = (action == YKUSH_UP ? 0x10 : 0x00) | port;
        int transferred;
        err = libusb_interrupt_transfer(hub, YKUSH_ENDPOINT_INT_OUT, buf,
            sizeof(buf), &transferred, TIMEOUT);
        if (err < 0) {
            fprintf(stderr, "Failed to send command: %s\n", libusb_error_name(err));
            goto out_dev;
        }
        err = libusb_interrupt_transfer(hub, YKUSH_ENDPOINT_INT_IN, buf,
            sizeof(buf), &transferred, TIMEOUT);
        if (err < 0) {
            fprintf(stderr, "Failed to receive response: %s\n", libusb_error_name(err));
            goto out_dev;
        }
        if (transferred < sizeof(buf)) {
            fprintf(stderr, "Short read from device.\n");
        }
    }

    out_dev:
    if (hub != NULL) {
        libusb_close(hub);
    }

    out_list:
    libusb_free_device_list(list, 1);

    libusb_exit(ctx);

    return err;

}

