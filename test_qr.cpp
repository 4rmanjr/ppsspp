#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <qrencode.h>

int main() {
    const char *payload = "ppsspp-sync://pair?host=192.168.1.50&port=27345&fp=SHA256:ab12cd34&pin=739281&name=MyPC";
    
    QRcode *qr = QRcode_encodeString(payload, 0, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (!qr) {
        printf("FAIL: QRcode_encodeString returned null\n");
        return 1;
    }
    
    int size = qr->width;
    printf("OK: QR generated, size=%dx%d modules\n", size, size);
    
    // Generate BMP
    int scale = 8;
    int imgSize = size * scale;
    int rowSize = ((imgSize + 31) / 32) * 4;
    int dataSize = rowSize * imgSize;
    int fileSize = 62 + dataSize;
    
    std::vector<uint8_t> bmp(fileSize, 0xFF);
    bmp[0] = 'B'; bmp[1] = 'M';
    *(uint32_t*)&bmp[2] = fileSize;
    *(uint32_t*)&bmp[10] = 62;
    *(uint32_t*)&bmp[14] = 40;
    *(int32_t*)&bmp[18] = imgSize;
    *(int32_t*)&bmp[22] = imgSize;
    *(uint16_t*)&bmp[26] = 1;
    *(uint16_t*)&bmp[28] = 1;
    *(uint32_t*)&bmp[34] = dataSize;
    
    uint8_t *pixels = bmp.data() + 62;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            bool black = (qr->data[y * size + x] & 0x01) != 0;
            if (black) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x * scale + sx;
                        int py = y * scale + sy;
                        int byteIdx = py * rowSize + (px / 8);
                        int bitIdx = 7 - (px % 8);
                        pixels[byteIdx] &= ~(1 << bitIdx);
                    }
                }
            }
        }
    }
    
    QRcode_free(qr);
    
    // Write BMP file
    FILE *f = fopen("/root/gitproject/ppsspp/test_qr.bmp", "wb");
    if (f) {
        fwrite(bmp.data(), 1, fileSize, f);
        fclose(f);
        printf("OK: BMP written to test_qr.bmp (%d bytes)\n", fileSize);
    } else {
        printf("FAIL: could not write BMP file\n");
        return 1;
    }
    
    printf("PASS: QR code generation test\n");
    return 0;
}
