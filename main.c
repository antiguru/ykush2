/*
Copyright 2015 Moritz Hoffmann <antiguru+deb@gmail.com>

This software is provided under the license specified in LICENSE.
*/

#include <assert.h>
#include <stdbool.h>
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
    int port= 0;
    int hub_id = 1;
    int err;
    enum action action = YKUSH_NONE;
    bool usb_init = false, dev_list = false, hub_open = false,
         claimed = false;
    
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
                err = EXIT_FAILURE;
                goto cleanup;
        }
    }
    
    if (!(action == YKUSH_DOWN || action == YKUSH_UP || action == YKUSH_LIST)) {
        print_usage();
        err = EXIT_FAILURE;
        goto cleanup;
    }

    if (action != YKUSH_LIST && (port != YKUSH_ALL_PORTS && (port < 1 && port > YKUSH_PORTS))) {
        print_usage();
        err = EXIT_FAILURE;
        goto cleanup;
    }

    err = libusb_init(&ctx);
    if (err < 0) {
        fprintf(stderr, "Failed to initialize USB: %s\n", libusb_strerror(err));
        err = EXIT_FAILURE;
        goto cleanup;
    }
    usb_init= true;
    
    libusb_device **list = NULL;
    ssize_t size = libusb_get_device_list(ctx, &list);
    if (size < 0) {
        fprintf(stderr, "Failed to enumerate devices: %s\n", libusb_strerror(err));
        err = EXIT_FAILURE;
        goto cleanup;
    }
    dev_list = true;

    libusb_device_handle *hub = NULL;
    int devices = 0;
    for (int i = 0; i < size; i++) {
        struct libusb_device_descriptor desc;
        err = libusb_get_device_descriptor(list[i], &desc);
        if (err < 0) {
            fprintf(stderr, "Failed to enumerate devices: %s\n", libusb_strerror(err));
            err = EXIT_FAILURE;
            goto cleanup;
        }
        if (desc.idVendor == YKUSH_USB_VENDOR_ID &&
            desc.idProduct == YKUSH_USB_PRODUCT_ID) {
            devices++;
            if (action == YKUSH_LIST) {
                printf("%i: %04x:%04x ", devices, desc.idVendor, desc.idProduct);
                err = libusb_open(list[i], &hub);
                if (err < 0) {
                    fprintf(stderr, "Unable to open device: %s\n", libusb_strerror(err));
                } else {
                    hub_open = true;
                    unsigned char serial[64];
                    unsigned char product[64];
                    unsigned char manufacturer[64];
                    err = libusb_get_string_descriptor_ascii(hub, desc.iSerialNumber, serial, sizeof(serial));
                    if (err < 0) {
                        fprintf(stderr, "Unable to query device: %s\n", libusb_strerror(err));
                        err = EXIT_FAILURE;
                        goto cleanup;
                    }
                    err = libusb_get_string_descriptor_ascii(hub, desc.iProduct, product, sizeof(product));
                    if (err < 0) {
                        fprintf(stderr, "Unable to query device: %s\n", libusb_strerror(err));
                        err = EXIT_FAILURE;
                        goto cleanup;
                    }
                    err = libusb_get_string_descriptor_ascii(hub, desc.iManufacturer, manufacturer, sizeof(manufacturer));
                    if (err < 0) {
                        fprintf(stderr, "Unable to query device: %s\n", libusb_strerror(err));
                        err = EXIT_FAILURE;
                        goto cleanup;
                    }
                    printf("%s/%s/%s", manufacturer, product, serial);
                    libusb_close(hub);
                    hub = NULL;
                    hub_open = false;
                }
                printf("\n");
            } else if (devices == hub_id) {
                err = libusb_open(list[i], &hub);
                if (err < 0) {
                    fprintf(stderr, "Failed to open device: %s\n", libusb_strerror(err));
                    err = EXIT_FAILURE;
                    goto cleanup;
                }
                hub_open = true;
            }
        }
    }
    
    if (action != YKUSH_LIST) {
        if (hub == NULL) {
            fprintf(stderr, "No hub with ID %i found.\n", hub_id);
            err = EXIT_FAILURE;
            goto cleanup;
        }
        
        err = libusb_detach_kernel_driver(hub, 0);
        if (err < 0 && err != LIBUSB_ERROR_NOT_FOUND) {
            fprintf(stderr, "Failed to detach kernel: %s\n", libusb_strerror(err));
            err = EXIT_FAILURE;
            goto cleanup;
        }

        err = libusb_set_configuration(hub, 1);
        if (err < 0) {
            fprintf(stderr, "Failed to set configuration: %s\n", libusb_strerror(err));
            err = EXIT_FAILURE;
            goto cleanup;
        }
    
        err = libusb_claim_interface(hub, 0);
        if (err < 0) {
            fprintf(stderr, "Failed to claim interface: %s\n", libusb_strerror(err));
            err = EXIT_FAILURE;
            goto cleanup;
        }
        claimed = true;
        
        unsigned char buf[6];
        
        buf[0] = (action == YKUSH_UP ? 0x10 : 0x00) | port;
        buf[1] = buf[0]; /* Upstream does this. */
        int transferred;
        err = libusb_interrupt_transfer(hub, YKUSH_ENDPOINT_INT_OUT, buf,
            sizeof(buf), &transferred, TIMEOUT);
        if (err < 0) {
            fprintf(stderr, "Failed to send command: %s\n", libusb_strerror(err));
            err = EXIT_FAILURE;
            goto cleanup;
        }
        err = libusb_interrupt_transfer(hub, YKUSH_ENDPOINT_INT_IN, buf,
            sizeof(buf), &transferred, TIMEOUT);
        if (err < 0) {
            fprintf(stderr, "Failed to receive response: %s\n", libusb_strerror(err));
            err = EXIT_FAILURE;
            goto cleanup;
        }
        if (transferred < sizeof(buf)) {
            fprintf(stderr, "Short read from device.\n");
            err = EXIT_FAILURE;
            goto cleanup;
        }
    }

cleanup:
    if (claimed) {
        libusb_release_interface(hub, 0);
    }
    if (hub_open) {
        assert(hub != NULL);
        libusb_reset_device(hub);
        libusb_close(hub);
    }
    if (dev_list) {
        libusb_free_device_list(list, 1);
    }
    if (usb_init) {
        libusb_exit(ctx);
    }

    return err;
}

