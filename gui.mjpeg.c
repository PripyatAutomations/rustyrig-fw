//
// Some crap chatgpt spewed to me that we should try for lulz
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <jpeglib.h>
#include "logger.h"
#include "state.h"
#include "gui.h"

#define WIDTH  320
#define HEIGHT 240

uint8_t framebuffer[WIDTH * HEIGHT * 3];  // RGB

void framebuffer_to_jpeg(FILE *out) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    jpeg_stdio_dest(&cinfo, out);
    cinfo.image_width = WIDTH;
    cinfo.image_height = HEIGHT;
    cinfo.input_components = 3;
    // XXX: We will have to do colorspace translate if not 24bpp
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 75, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    JSAMPROW row_pointer;
    while (cinfo.next_scanline < HEIGHT) {
        row_pointer = &framebuffer[cinfo.next_scanline * WIDTH * 3];
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
}

void handle_client(int client_sock) {
    FILE *client = fdopen(client_sock, "w");
    fprintf(client, "HTTP/1.1 200 OK\r\n");
    fprintf(client, "Content-Type: multipart/x-mixed-replace; boundary=FRAME\r\n\r\n");

    while (1) {
        fprintf(client, "--FRAME\r\n");
        fprintf(client, "Content-Type: image/jpeg\r\n\r\n");
        framebuffer_to_jpeg(client);
        fprintf(client, "\r\n");

        fflush(client);
        usleep(100000);  // 10 FPS
    }
}


#if	0
int main() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(8080) };

    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_sock, 5);

    while (1) {
        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock >= 0) {
            if (!fork()) {  // Handle in child process
                handle_client(client_sock);
                close(client_sock);
                exit(0);
            }
            close(client_sock);
        }
    }
}
#endif
