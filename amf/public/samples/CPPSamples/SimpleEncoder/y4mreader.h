#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
class y4mreader
{
protected:
    FILE* fp;
    int32_t par_width, par_height;
    uint64_t next_frame;
    size_t seq_header_len, frame_header_len;
    uint64_t frame_size;
    uint64_t csp;
    uint8_t* ptmp_line_buff;
public:
    int32_t width, height;
    uint32_t fps_num;
    uint32_t fps_den;

	y4mreader();
	virtual ~y4mreader();

	int open_file_y4m(char* filename);
    uint64_t y4mreader::read_frame_YUV420(uint8_t* picY,
        uint8_t* picU,
        uint8_t* picV,
        int32_t pitchY,
        int32_t pitchUV,
        uint64_t framenum);
    uint64_t read_frame_NV12(uint8_t* picY,
        uint8_t* picUV,
        int32_t pitchY,
        int32_t pitchUV,
        uint64_t framenum);
	int close_file_y4m();
};

