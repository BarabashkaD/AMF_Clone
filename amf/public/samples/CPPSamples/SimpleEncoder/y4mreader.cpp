#include "y4mreader.h"
#include "inttypes.h"
const std::string Y4M_MAGIC = "YUV4MPEG2";
const int32_t MAX_YUV4_HEADER = 80;
const std::string Y4M_FRAME_MAGIC = "FRAME";
const int32_t MAX_FRAME_HEADER = 80;

/* Taken directly from x264 */
void reduce_fraction(int* n, int* d)
{
    int a = *n;
    int b = *d;
    int c;
    if (!a || !b)
        return;
    c = a % b;
    while (c) {
        a = b;
        b = c;
        c = a % b;
    }
    *n /= b;
    *d /= b;
}

y4mreader::y4mreader(): fp(nullptr),
                        width(0), height(0),
                        par_width(0), par_height(0),
                        next_frame(0),
                        seq_header_len(0), frame_header_len(0),
                        frame_size(0),
                        csp(0),
                        fps_num(0),
                        fps_den(0),
    ptmp_line_buff(nullptr)
{

}

y4mreader::~y4mreader()
{
    close_file_y4m();
    if (ptmp_line_buff != nullptr)
        delete ptmp_line_buff;
    ptmp_line_buff = nullptr;
}
int y4mreader::open_file_y4m(char* filename)
{
    int i, n, d;
    int interlaced;
    char header[MAX_YUV4_HEADER + 10];
    char* tokstart, * tokend, * header_end;
    
    next_frame = 0;

    if (!strcmp(filename, "-"))
        fp = stdin;
    else
        fp = fopen(filename, "rb");
    if (fp == NULL)
        return -1;

    frame_header_len = Y4M_FRAME_MAGIC.length() + 1;

    /* Read header */
    for (i = 0; i < MAX_YUV4_HEADER; i++) {
        header[i] = fgetc(fp);
        if (header[i] == '\n') {
            /* Add a space after last option. Makes parsing "444" vs
               "444alpha" easier. */
            header[i + 1] = 0x20;
            header[i + 2] = 0;
            break;
        }
    }
    if (i == MAX_YUV4_HEADER || strncmp(header, Y4M_MAGIC.c_str(), Y4M_MAGIC.length()))
        return -1;

    /* Scan properties */
    header_end = &header[i + 1];        /* Include space */
    seq_header_len = i + 1;
    for (tokstart = &header[Y4M_MAGIC.length() + 1]; tokstart < header_end; tokstart++) {
        if (*tokstart == 0x20)
            continue;
        switch (*tokstart++) {
        case 'W':              /* Width. Required. */
            width = strtol(tokstart, &tokend, 10);
            tokstart = tokend;
            break;
        case 'H':              /* Height. Required. */
            height = strtol(tokstart, &tokend, 10);
            tokstart = tokend;
            break;
        case 'C':              /* Color space */
            if (strncmp("420", tokstart, 3)) {
                fprintf(stderr, "Colorspace unhandled\n");
                return -1;
            }
            tokstart = strchr(tokstart, 0x20);
            break;
        case 'I':              /* Interlace type */
            switch (*tokstart++) {
            case 'p':
                interlaced = 0;
                break;
            case '?':
            case 't':
            case 'b':
            case 'm':
            default:
                interlaced = 1;
                fprintf(stderr, "Warning, this sequence might be interlaced\n");
            }
            break;
        case 'F': /* Frame rate - 0:0 if unknown */
                  /* Frame rate in unimportant. */
            if (sscanf(tokstart, "%d:%d", &n, &d) == 2 && n && d) {
                reduce_fraction(&n, &d);
                fps_num = n;
                fps_den = d;
            }
            tokstart = strchr(tokstart, 0x20);
            break;
        case 'A': /* Pixel aspect - 0:0 if unknown */
                  /* Don't override the aspect ratio if sar has been explicitly set on the commandline. */
            if (sscanf(tokstart, "%d:%d", &n, &d) == 2 && n && d) {
                reduce_fraction(&n, &d);
                par_width = n;
                par_height = d;
            }
            tokstart = strchr(tokstart, 0x20);
            break;
        case 'X': /* Vendor extensions */
            if (!strncmp("YSCSS=", tokstart, 6)) {
                /* Older nonstandard pixel format representation */
                tokstart += 6;
                if (strncmp("420JPEG", tokstart, 7) &&
                    strncmp("420MPEG2", tokstart, 8) &&
                    strncmp("420PALDV", tokstart, 8)) {
                    fprintf(stderr, "Unsupported extended colorspace\n");
                    return -1;
                }
            }
            tokstart = strchr(tokstart, 0x20);
            break;
        }
    }
    ptmp_line_buff = new uint8_t[width];
    fprintf(stderr, "yuv4mpeg: %ix%i@%i/%ifps, %i:%i\n",width, height, fps_num, fps_den,par_width, par_height);
    return 0;
}

uint64_t y4mreader::read_frame_YUV420( uint8_t* picY,
                               uint8_t* picU, 
                               uint8_t* picV,
                               int32_t pitchY,
                               int32_t pitchUV, 
                                uint64_t framenum)
{
    size_t slen = Y4M_FRAME_MAGIC.length();
    int i = 0;
    char header[16];
    
    if (framenum != next_frame) {
        if (fseek(fp, (uint64_t)framenum * (3 * (width * height) / 2 + frame_header_len) + seq_header_len, SEEK_SET))
            return -1;
    }

    /* Read frame header - without terminating '\n' */
    if (fread(header, 1, slen, fp) != slen)
        return -1;

    header[slen] = 0;
    if (strncmp(header, Y4M_FRAME_MAGIC.c_str(), slen)) {
        fprintf(stderr, "Bad header magic (%" PRIx32 " <=> %s)\n",*((uint32_t*)header), header);
        return -1;
    }

    /* Skip most of it */
    while (i < MAX_FRAME_HEADER && fgetc(fp) != '\n')
        i++;
    if (i == MAX_FRAME_HEADER) {
        fprintf(stderr, "Bad frame header!\n");
        return -1;
    }
    frame_header_len = i + slen + 1;

    try
    {
        for (int32_t y = 0; y < height; y++)
        {
            uint8_t* pDataLine = picY + y * pitchY;
            if (fread(pDataLine, 1, width, fp) <= 0)
                throw std::exception("File end on middle of Y compoment");
        }
        for (int32_t y = 0; y < height/2; y++)
        {
            uint8_t* pDataLine = picU + y * pitchUV;
            if (fread(pDataLine, 1, width/2, fp) <= 0)
                throw std::exception("File end on middle of U compoment");
        }
        for (int32_t y = 0; y < height / 2; y++)
        {
            uint8_t* pDataLine = picV + y * pitchUV;
            if (fread(pDataLine, 1, width / 2, fp) <= 0)
                throw std::exception("File end on middle of V compoment");
        }
    }
    catch (const std::exception& e)
    {
        return 0;
    }
    
    next_frame = framenum + 1;

    return framenum;
}
uint64_t y4mreader::read_frame_NV12(uint8_t* picY,
    uint8_t* picUV,
    int32_t pitchY,
    int32_t pitchUV,
    uint64_t framenum)
{
    size_t slen = Y4M_FRAME_MAGIC.length();
    int i = 0;
    char header[16];

    if (framenum != next_frame) {
        if (fseek(fp, (uint64_t)framenum * (3 * (width * height) / 2 + frame_header_len) + seq_header_len, SEEK_SET))
            return -1;
    }

    /* Read frame header - without terminating '\n' */
    if (fread(header, 1, slen, fp) != slen)
        return -1;

    header[slen] = 0;
    if (strncmp(header, Y4M_FRAME_MAGIC.c_str(), slen)) {
        fprintf(stderr, "Bad header magic (%" PRIx32 " <=> %s)\n", *((uint32_t*)header), header);
        return -1;
    }

    /* Skip most of it */
    while (i < MAX_FRAME_HEADER && fgetc(fp) != '\n')
        i++;
    if (i == MAX_FRAME_HEADER) {
        fprintf(stderr, "Bad frame header!\n");
        return -1;
    }
    frame_header_len = i + slen + 1;

    try
    {
        for (int32_t y = 0; y < height; y++)
        {
            uint8_t* pDataLine = picY + y * pitchY;
            if (fread(pDataLine, 1, width, fp) <= 0)
                throw std::exception("File end on middle of Y compoment");
        }
        for (int32_t y = 0; y < height / 2; y++)
        {
            if (fread(ptmp_line_buff, 1, width / 2, fp) <= 0)
                throw std::exception("File end on middle of U compoment");
            uint8_t* pDataLine = picUV + y * pitchUV;
            for (int32_t w = 0; w < width / 2; w++)
            {
                pDataLine[w * 2] = ptmp_line_buff[w];
            }
        }
        for (int32_t y = 0; y < height / 2; y++)
        {
            if (fread(ptmp_line_buff, 1, width / 2, fp) <= 0)
                throw std::exception("File end on middle of U compoment");
            uint8_t* pDataLine = picUV + 1 + y * pitchUV;
            for (int32_t w = 0; w < width / 2; w++)
            {
                pDataLine[w * 2] = ptmp_line_buff[w];
            }
        }
    }
    catch (const std::exception& e)
    {
        return -1;
    }

    next_frame = framenum + 1;

    return framenum;
}

int y4mreader::close_file_y4m()
{
    if (!fp)
        return 0;
    fclose(fp);
    fp = nullptr;
    return 0;
}