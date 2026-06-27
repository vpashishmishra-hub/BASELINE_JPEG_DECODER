#include<stdio.h>
#include<stdlib.h>
#include<stdbool.h>
#include <stdint.h>
#include<math.h>
#include<string.h>

#define SOI   0xD8
#define EOI   0xD9
#define TEM   0x01
#define APP0  0xE0
#define APP15 0xEF
#define DQT   0xDB
#define SOF0  0xC0
#define DHT   0xC4
#define SOS   0xDA
#define DRI   0xDD

struct huffman_table {
    uint8_t symbols[256];    
    int32_t min_code[17];    
    int32_t max_code[17];   
    int32_t valptr[17];         
};


struct component {
    int id;
    int h_sample;
    int v_sample;
    int quant_table_id;
	int blocks_per_mcu_h;    // NEW: how many blocks wide in one MCU
    int blocks_per_mcu_v;
	int dc_table_id;
	int ac_table_id;
	
};

struct jpeg_image {
    bool soi_valid;
    int quant_tables[4][8][8];
    bool quant_table_present[4];

    int precision;
    int height;
    int width;
    int num_components;
    struct component components[4];
	
	 int max_h;               // new max sampling factors
    int max_v; 

    int mcus_across;
    int mcus_down;
	
	
	 struct huffman_table dc_huff_tables[4];  // Slots for tables 0 to 3
    struct huffman_table ac_huff_tables[4];
	
	 uint8_t *clean_bitstream;  // pure entropy-coded data buffer
    int clean_stream_length;   //  pure data bytes were collected
	
	int bit_posn;
	int byte_posn;
	
	int *dct_coeffs[3];

};

int bit_reader (struct jpeg_image *img){

if(img->byte_posn > img->clean_stream_length){
		printf("error in bit reader 1st line");
	return 0;
}

int extracted_bit; 
	extracted_bit= ( *(img->clean_bitstream + img->byte_posn)  >>img->bit_posn) & 1;

img->bit_posn--;

if(img->bit_posn<0){
	img->bit_posn=7;
	img->byte_posn++;
}	
return extracted_bit;

}

int huffman_matching (struct jpeg_image *img, struct huffman_table *table){
	
	int collected_bit=0;
	
	for(int lentgh =1;  lentgh<17 ; lentgh++){
		int bit= bit_reader(img);
		collected_bit= (collected_bit <<1) | bit ;
		
		if(table->max_code[lentgh] == -1){
			continue;
		}
		if(collected_bit <= table->max_code[lentgh]){
			int index= table->valptr[lentgh] + (collected_bit - table->min_code[lentgh]);
			
	return table->symbols[index];
		}
		
	}
	 printf(" end of huffman loop without a match!\n");
    return -1;
}

int read_signed_mod(struct jpeg_image *img , int size){

if(size==0){
	return 0;
}
int bits=0;

for(int i =0;  i<size;  i++ ){
	int bit= bit_reader(img);
	bits=(bits<<1) | bit;
}
int top_bit= (bits>>(size-1));

if(top_bit==1){
	return bits;
}
else{
	int offset= (1<<size)-1;
	return (bits-offset);
}


}

int  decode_dc(struct jpeg_image* img, struct huffman_table *dc_table , int * dc_val ){
	
	int size = huffman_matching(img, dc_table);
	int diff = read_signed_mod(img, size);
	*dc_val= *dc_val + diff;
	
	return *dc_val;
}

int decode_ac (struct jpeg_image *img, struct huffman_table *ac_table, int* out_block){
    int k = 1; 
    
    while (k < 64) {
        int symbol = huffman_matching(img, ac_table);
        
        if (symbol == 0x00) {
            while (k < 64) {
                out_block[k] = 0;
                k++;
            }
            break; 
        }
              
        if (symbol == 0xF0) {
            int end = k + 16;
            while (k < end && k < 64) {
                out_block[k] = 0;
                k++;
            }
            continue; 
        }
        
        int run  = (symbol >> 4) & 0x0F;
        int size = symbol & 0x0F;
        
        int end = k + run;
        while (k < end && k < 64) {
            out_block[k] = 0;
            k++;
        }
        
        if (k < 64) {
            out_block[k] = read_signed_mod(img, size);
            k++; 
        }
    }
    return 0;
}









long get_jpeg_file_size(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        return -1; // File could not be opened
    }

    // Move the file pointer to the very end of the file
    fseek(file, 0, SEEK_END);
    
    // Get the current position of the pointer (which equals total bytes)
    long size = ftell(file);
    
    fclose(file);
    return size;
}

bool check_lentgh(unsigned char marker){
	
	if(marker >= 0xD0 && marker<= 0xD7){
		return false;
		}
	
	if(marker == SOI || marker== EOI || marker== TEM){
		return false;
		}
		
	else{
	return true;
	}
}

// ACTUALLY THIS UNZIGZAGS THE DCT AND QUANT TABLE

void linear2zigzag(int *raw, int zig_zag_table[8][8]) {
   
   const int f[8][8] = {
        { 0,  1,  5,  6, 14, 15, 27, 28 },
        { 2,  4,  7, 13, 16, 26, 29, 42 },
        { 3,  8, 12, 17, 25, 30, 41, 43 },
        { 9, 11, 18, 24, 31, 40, 44, 53 },
        {10, 19, 23, 32, 39, 45, 52, 54 },
        {20, 22, 33, 38, 46, 51, 55, 60 },
        {21, 34, 37, 47, 50, 56, 59, 61 },
        {35, 36, 48, 49, 57, 58, 62, 63 }
    };


    for(int i = 0; i < 8; i++) {
        for(int j = 0; j < 8; j++) {
            zig_zag_table[i][j] = raw[f[i][j]];
        }
    }
}

void dqt_reader(unsigned char *buffer, size_t bytes_read, int dqt_pos, struct jpeg_image *img){
	
	int lentgh=(buffer[dqt_pos + 2] << 8) | buffer[dqt_pos + 3];
	
	int precision = (buffer[dqt_pos+4] >> 4) & 0x0F;
    int id = buffer[dqt_pos+4] & 0x0F;
	
	printf("  lentgh field: %d\n", lentgh);
	printf("%d is the precision of dqt at dqt pos %d  \n", precision, dqt_pos);
	printf("%d is the id of dqt at dqt pos %d  \n", id, dqt_pos);
	
	
	printf("\n \n dqt content of dqt posn %d starts from here ", dqt_pos);
	int start_index=dqt_pos+5;
	int end_index=dqt_pos +2 +lentgh;
	
	img->quant_table_present[id]=true;
	int raw[64]; int i=0;
	
	while(start_index < end_index){
		printf("%d \n", buffer[start_index]);
		raw[i]=buffer[start_index];
		start_index++;
		i++;
	}
	
	linear2zigzag(raw, img->quant_tables[id]);
}

void sof0_reader(unsigned char *buffer, size_t bytes_read, int sof0_pos, struct jpeg_image *img){
	
	int lentgh = (buffer[sof0_pos+2] << 8) | buffer[sof0_pos+3];
	int precision = buffer[sof0_pos+4];
	int height = (buffer[sof0_pos+5] <<8 ) | buffer[sof0_pos+6];
    int width = (buffer[sof0_pos+7] <<8 ) | buffer[sof0_pos+8];
	int color_channel_count=buffer[sof0_pos+9];
	
	img->precision= precision;
	img->height=height;
	img->width=width;
	img->num_components=color_channel_count;
	
	printf("\n \n sof0 reader function \n \n");
	printf("lentgh of sof0 is %d \n", lentgh);
	printf("precision of sof0 is %d \n", precision);
	printf("height of sof0 is %d \n", height);	
	printf("width of sof0 is %d \n", width);	
	printf("color_channel_count of sof0 is %d \n", color_channel_count);
	
	sof0_pos= sof0_pos + 10;
	int itr=0;
	
	while(itr<3){
		int comp_id= buffer[sof0_pos];
		int horizontal_sampling_factor= ( (buffer[sof0_pos+1] >> 4) & 0x0F );
		int vertical_sampling_factor= ( (buffer[sof0_pos+1] ) & 0x0F );
		int quant_table_id = buffer[sof0_pos+2];
		
		img->components[itr].id= comp_id;
		img->components[itr].h_sample=horizontal_sampling_factor;
		img->components[itr].v_sample=vertical_sampling_factor;
		img->components[itr].quant_table_id=quant_table_id;
		
		sof0_pos= sof0_pos+3;
		itr++;
		 printf("comp id is %d \n", comp_id);
		 printf("horizontal_sampling_factor is %d \n", horizontal_sampling_factor);
		 printf("vertical_sampling_factor is %d \n", vertical_sampling_factor);
		 printf("quant_table_id is %d \n", quant_table_id);		

    }
	
	int max_h = 0;
    int max_v = 0;
    for (int i = 0; i < img->num_components; i++) {
        if (img->components[i].h_sample > max_h) max_h = img->components[i].h_sample;
        if (img->components[i].v_sample > max_v) max_v = img->components[i].v_sample;
		
	}
    img->max_h = max_h;
    img->max_v = max_v;

    int mcu_pixel_width  = max_h * 8;
    int mcu_pixel_height = max_v * 8;
    
    img->mcus_across = (img->width  + mcu_pixel_width  - 1) / mcu_pixel_width;
    img->mcus_down   = (img->height + mcu_pixel_height - 1) / mcu_pixel_height;

   
    for (int i = 0; i < img->num_components; i++) {
        img->components[i].blocks_per_mcu_h = img->components[i].h_sample;
        img->components[i].blocks_per_mcu_v = img->components[i].v_sample;
    }

  
    for (int i = 0; i < img->num_components; i++) {
        if (img->components[i].h_sample < 1 || img->components[i].h_sample > 4 ||
            img->components[i].v_sample < 1 || img->components[i].v_sample > 4) {
            printf("ERROR: component %d has unsupported sampling %dx%d\n",
                   img->components[i].id, img->components[i].h_sample, img->components[i].v_sample);
            return;
        }
    }
	
	printf("\n--- TARGET 3 DIAGNOSTICS ---\n");
    printf("Max Sampling Factors: Max_H = %d, Max_V = %d\n", img->max_h, img->max_v);
    printf("MCU Pixel Footprint: %dx%d pixels\n", mcu_pixel_width, mcu_pixel_height);
    printf("Calculated Macro Grid: %d MCUs across x %d MCUs down\n", img->mcus_across, img->mcus_down);
    
    for (int i = 0; i < img->num_components; i++) {
        printf("Component ID %d Layout: [%dx%d] blocks per MCU\n", 
               img->components[i].id, 
               img->components[i].blocks_per_mcu_h, 
               img->components[i].blocks_per_mcu_v);
    }
    printf("----------------------------\n\n");
	
	}
	
void dht_reader(const char *buffer, int bytes_read, int dht_posn, struct jpeg_image *img){
	
	
	uint16_t lentgh = ((buffer[dht_posn + 2] & 0xFF) << 8) | (buffer[dht_posn + 3] & 0xFF);
	printf("lentgh of dht section is %d  \n", lentgh);

	int remaining_payload = lentgh - 2;
	
	int p = dht_posn + 4;

		while (remaining_payload > 0) {
		
		int table_class = (buffer[p] >> 4) & 0x0F;
		int table_id = buffer[p] & 0x0F;
		
		if (table_class == 0){
			printf(" DC \n");
		}
		else {
			printf(" AC \n  ");
		}
		printf("table id is %d \n \n \n ", table_id);
		p++;
		remaining_payload--;

		int index[17];
		for (int i = 1; i <= 16; i++) {
			index[i] = buffer[p] ;
			p++;
			remaining_payload--;
		}


		int total_symbols = 0;
		for (int i = 1; i <= 16; i++) {
			total_symbols += index[i];
		}

		for (int i = 0; i < total_symbols; i++) {
			if (table_class == 0) {
				img->dc_huff_tables[table_id].symbols[i] = buffer[p] & 0xFF;
			} else { 
				img->ac_huff_tables[table_id].symbols[i] = buffer[p] & 0xFF; 
			}
			p++;
			remaining_payload--;
		}

		printf("\n");	
		printf(" starting the decoding \n");
		
		
		int code = 0;
		int symbol_index = 0;
		
		for (int j = 1; j < 17; j++) {
			int num_codes = index[j];
			
			if (num_codes == 0) {
				if (table_class == 0) {
					img->dc_huff_tables[table_id].max_code[j] = -1;
				} else {
					img->ac_huff_tables[table_id].max_code[j] = -1;
				}
				code = code << 1;	
				continue;
			}

			int min_code_for_index = code;
			int starting_symbol_for_index = symbol_index;
			int max_code_for_index = code + num_codes - 1;
			
			if (table_class == 0) {
				img->dc_huff_tables[table_id].min_code[j] = min_code_for_index;
				img->dc_huff_tables[table_id].max_code[j] = max_code_for_index;
				img->dc_huff_tables[table_id].valptr[j]   = starting_symbol_for_index;
			} else {
				img->ac_huff_tables[table_id].min_code[j] = min_code_for_index;
				img->ac_huff_tables[table_id].max_code[j] = max_code_for_index;
				img->ac_huff_tables[table_id].valptr[j]   = starting_symbol_for_index;
			}
			
			symbol_index = symbol_index + num_codes;
			code = (max_code_for_index + 1) << 1;
		}
		

	}
}

void sos_reader (unsigned char *buffer, size_t bytes_read, int sos_posn, struct jpeg_image *img){
	
	printf("\n \n ");
	int lentgh=(buffer[sos_posn+2] <<8) | (buffer[sos_posn+3]);
	int scan_comp = buffer[sos_posn+4];
	sos_posn= sos_posn+5; 
	printf("lentgh is %d \n ", lentgh);
	printf("scan_comp is %d \n ", scan_comp);
	
	
	int i=scan_comp;
	
	while(i > 0){
	int component_id= buffer[sos_posn];
	sos_posn++;
	int dc_table_id=(buffer[sos_posn] >>4) & (0X0F);
	int ac_table_id=(buffer[sos_posn] ) & (0X0F);
	sos_posn++;
	
	printf("\n component id is %d ", component_id);
  	printf("\n dc table  id is %d ", dc_table_id);
  	printf("\n ac table  id is %d ", ac_table_id);
	
	 for (int c = 0; c < img->num_components; c++) {
        if (img->components[c].id == component_id) {
            // Save the active tables to your context tracking arrays
            img->components[c].dc_table_id = dc_table_id;
            img->components[c].ac_table_id = ac_table_id;
            break;
        }
    }

	
	i--;
}
printf("\n \n ");

int spec_start = buffer[sos_posn];
int spec_end   = buffer[sos_posn + 1];
int succ_approx = buffer[sos_posn + 2];

if (spec_start != 0 || spec_end != 63 || succ_approx != 0) {
    printf("ERROR: Unsupported Progressive JPEG formatting! Only Baseline Sequential supported.\n");
    return;
}

// Advance past the 3 fixed spectral bytes to land right at the bitstream start
sos_posn += 3; 

printf("sos_posn before malloc = %d, bytes_read = %zu\n", sos_posn, bytes_read);
img->clean_bitstream = malloc(bytes_read - sos_posn);
int write_pos = 0;
int read_pos = sos_posn;

while (read_pos < bytes_read) {
	
    if (buffer[read_pos] != 0xFF) {
        // Standard data byte: copy it directly to your clean buffer array
        img->clean_bitstream[write_pos] = buffer[read_pos];
        write_pos++;
        read_pos++;
    } 
    else {
        // We hit an 0xFF indicator flag. Look at the trailing byte next to it.
        unsigned char next_byte = buffer[read_pos + 1];

        if (next_byte == 0x00) {
            // Rule: Byte Stuffing. Copy only the 0xFF, discard the fake 0x00 padding.
            img->clean_bitstream[write_pos] = 0xFF;
            write_pos++;
            read_pos += 2; // Jump past BOTH the 0xFF and the 0x00 in the file stream
        } 
        else if (next_byte == 0xD9) {
            // Rule: EOI (End of Image) reached! The pixel bitstream is complete.
            printf("Found EOI marker 0xFFD9 inside SOS scanner. Stream extraction complete!\n");
            break;
        } 
        else if (next_byte >= 0xD0 && next_byte <= 0xD7) {
            // Rule: Restart Markers (Checkpoint signals). Skip them entirely.
            read_pos += 2; 
        } 
        else if (next_byte == 0xFF) {
            // Rule: Double 0xFF padding. Treat the first one as empty alignment and slide forward.
            read_pos++;
        }
}
    }


img->clean_stream_length = write_pos;
printf(" \n \n \n \n \n Total clean, unstuffed bitstream bytes extracted to RAM: %d\n", write_pos);


}


void marker_printer(unsigned char *buffer, size_t bytes_read, struct jpeg_image *img) {
    int posn = 0;
    while (posn < bytes_read) {

        if (posn + 1 >= bytes_read) {
            printf("File ended unexpectedly, closing function\n");
            break;
        }

        if (buffer[posn] != 0xFF) {
            printf("Lost sync at offset %d: got %02X\n", posn, buffer[posn]);
            break;
        }

        unsigned char marker = buffer[posn + 1];
        printf("offset %d: MARKER %02X\n", posn, marker);
		
		if (marker == SOS) {
        printf("SOS reached, scan data follows \n");
		sos_reader(buffer, bytes_read, posn, img);
        break;
       }

        if (marker == EOI) {
            printf("EOI reached, stopping\n");
            break;
        }
		
		        if (marker == DQT) {
            printf("DQT FUNCTION STARTS HERE \n \n");
            dqt_reader(buffer, bytes_read, posn, img);
        }
		
		if (marker == SOF0) {
            printf("SOF0 FUNCTION STARTS HERE \n \n");
            sof0_reader(buffer, bytes_read, posn, img);
        }
		
				if (marker == DHT)					{
            printf("DHT FUNCTION STARTS HERE \n \n");
            dht_reader(buffer, bytes_read, posn,img);
        }



        if (!check_lentgh(marker)) {
            posn = posn + 2;
            continue;
        }

        if (posn + 3 >= bytes_read) {
            printf("Truncated file: can't read length at offset %d\n", posn);
            break;
        }

        int length = (buffer[posn + 2] << 8) | buffer[posn + 3];
        printf("  length field: %d\n", length);
        posn = posn + 2 + length;
    }
}

void dequantizer(int *current_block, int quant_table[8][8], int dequantized[8][8]) {
    int temp[8][8];
    linear2zigzag(current_block, temp);

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            dequantized[row][col] = temp[row][col] * quant_table[row][col];
        }
    }
}


void idct(int in [8][8], float out [8][8]){
	 const double pi = 3.14159265358979323846;
	 double cu;
	 double cv;
	for (int x=0; x<8; x++){
			for (int y=0; y<8; y++){
				double fxy= 0;
			for (int u=0; u<8; u++){
			for (int v=0; v<8; v++){
				if(u==0){
					  cu=0.70721357;
				}
				else{
					  cu=1;
				}
				if(v==0){
					  cv=0.70721357;
				}
				else{
					  cv=1;
				}				
	         fxy= fxy+(cu*cv*in[u][v]*cos(((2*x+1)*u*pi)/16)*cos(((2*y+1)*v*pi)/16));
			}
			}
			out[x][y]= (float)fxy/4 ;  // 
			}
	}
	
}


unsigned char clamper(float x){
	
	if (x>255){
		return 255;
	}
	if(x<0){
		return 0;
	}
	
	x= (unsigned char) x;
	return x;
	
}

void ycbcr_to_rgb (unsigned char y, unsigned char cb , unsigned char cr , unsigned char * rgb){
	
	float r= y+ 1.402*(cr-128); 
	float g= y  -(0.344136*(cb-128)) -(0.714136*(cr-128))  ; 
	float b= y+ 1.772*(cb-128); 
	
	r= clamper(r);
    g= clamper(g);
    b= clamper(b);
	  
	 // BMP REQUIRES THE ORDER  BGR
	rgb[0]=b;
	rgb[1]=g;
	rgb[2]=r;
	
}


void write_bmp(const char *filename, int width, int height, unsigned char *rgb_data) {
    int row_stride = (width * 3 + 3) & ~3;
    unsigned char *pad_row = (unsigned char*)calloc(row_stride, 1);
    if (!pad_row) {
        printf("Memory error writing BMP\n");
        return;
    }

    FILE *fout = fopen(filename, "wb");
    if (!fout) {
        printf("Cannot open %s for writing\n", filename);
        free(pad_row);
        return;
    }

    unsigned char file_header[14] = {
        'B','M',
        0,0,0,0,
        0,0,0,0,
        54,0,0,0
    };
    unsigned char info_header[40] = {
        40,0,0,0,
        0,0,0,0,
        0,0,0,0,
        1,0,
        24,0,
        0,0,0,0,
        0,0,0,0,
        0,0,0,0,
        0,0,0,0,
        0,0,0,0,
        0,0,0,0
    };

    int file_size = 54 + row_stride * height;
    memcpy(file_header + 2, &file_size, 4);
    memcpy(info_header + 4, &width, 4);
    memcpy(info_header + 8, &height, 4);

    fwrite(file_header, 1, 14, fout);
    fwrite(info_header, 1, 40, fout);

    for (int y = height - 1; y >= 0; y--) {
        unsigned char *row = rgb_data + y * width * 3;
        memcpy(pad_row, row, width * 3);
        fwrite(pad_row, 1, row_stride, fout);
    }

    fclose(fout);
    free(pad_row);
    printf("BMP saved: %s\n", filename);
}

int main(){

FILE*fptr;

fptr=fopen("scenery.jpg","rb");

if(fptr== NULL){
	printf("error in opening file");
	printf("\n");
	return EXIT_FAILURE;
}

long size=get_jpeg_file_size("scenery.jpg");

// WITH THIS (Allocates memory safely on the heap):
unsigned char *buffer = (unsigned char *)malloc(size);
if (buffer == NULL) {
    printf("Error: Out of memory\n");
    fclose(fptr);
    return EXIT_FAILURE;
}


size_t bytes_read= fread( buffer, 1, size, fptr);


if(buffer[0] == 0xFF && buffer[1] == SOI){
	printf("SOI veirifed");
	printf("\n");
}

else{
	printf(" SOI not verified");
	return EXIT_FAILURE;
}

struct jpeg_image img;
img.bit_posn=7;
img.byte_posn=0;


marker_printer(buffer,bytes_read, &img);

printf("\n"); printf("\n"); printf("\n");
printf("width=%d height=%d\n", img.width, img.height);
printf("mcus_across=%d mcus_down=%d\n", img.mcus_across, img.mcus_down);
printf("component 0 sampling: %dx%d\n", img.components[0].h_sample, img.components[0].v_sample);
printf("component 1 sampling: %dx%d\n", img.components[1].h_sample, img.components[1].v_sample);
printf("component 2 sampling: %dx%d\n", img.components[2].h_sample, img.components[2].v_sample);
printf("\n"); printf("\n"); printf("\n");

printf("\n"); printf("\n"); printf("\n");
printf("table 0 is ");
printf("\n");

 for(int i = 0; i < 8; i++) {
        for(int j = 0; j < 8; j++) {
            printf("%d ",img.quant_tables[0][i][j]);
        }
		printf("\n");
    }
	
printf("\n"); printf("\n"); printf("\n");
printf("table 1 is ");
printf("\n");

 for(int i = 0; i < 8; i++) {
        for(int j = 0; j < 8; j++) {
            printf("%d ",img.quant_tables[1][i][j]);
        }
		printf("\n");
    }	


int total_blocks_Y = (img.mcus_across * img.components[0].blocks_per_mcu_h) * 
                     (img.mcus_down * img.components[0].blocks_per_mcu_v);
img.dct_coeffs[0] = (int *)malloc(total_blocks_Y * 64 * sizeof(int));


int total_blocks_Cb = (img.mcus_across * img.components[1].blocks_per_mcu_h) * 
                      (img.mcus_down * img.components[1].blocks_per_mcu_v);
img.dct_coeffs[1] = (int *)malloc(total_blocks_Cb * 64 * sizeof(int));


int total_blocks_Cr = (img.mcus_across * img.components[2].blocks_per_mcu_h) * 
                      (img.mcus_down * img.components[2].blocks_per_mcu_v);
img.dct_coeffs[2] = (int *)malloc(total_blocks_Cr * 64 * sizeof(int));


// --- START OF TARGET 5 MASTER INTERATION GRID LOOP ---

int width = img.width;
int height = img.height;
int total_pixels= width*height;

unsigned char * Y_PLANE = (unsigned char*)malloc( total_pixels * sizeof(unsigned char));
 if (Y_PLANE== NULL){
	 printf("memory for Y plane not allocated \n");
 }
 
unsigned char * CB_PLANE= (unsigned char*)malloc( total_pixels * sizeof(unsigned char));
 if (CB_PLANE== NULL){
	 printf("memory for CB_PLANE plane not allocated \n");
 }
 
unsigned char * CR_PLANE= (unsigned char*)malloc( total_pixels * sizeof(unsigned char));
 if (CR_PLANE== NULL){
	 printf("memory for CR_PLANE plane not allocated \n");
 }

     int y_qt_id  = img.components[0].quant_table_id;
    int cb_qt_id = img.components[1].quant_table_id;
    int cr_qt_id = img.components[2].quant_table_id;

     int dc_pred_Y  = 0;
    int dc_pred_Cb = 0;
    int dc_pred_Cr = 0;


for (int mcu_y = 0; mcu_y < img.mcus_down; mcu_y++) {
    for (int mcu_x = 0; mcu_x < img.mcus_across; mcu_x++) {
	
	int y_dc_table_id= img.components[0].dc_table_id;
		int y_ac_table_id= img.components[0].ac_table_id;
	
	for(int b=0; b<4 ; b++){
		
		int block_x= mcu_x*2 + b%2 ;
		int block_y= mcu_y*2 + b/2 ;
		int block_no_Y= (block_y*2*img.mcus_across) + block_x;   // for a square n * n matrix if you want a flat index then flt_idx= current_row*width_of_matrix +current column
		int * Y_block = &img.dct_coeffs[0][block_no_Y*64];
		
		Y_block[0]= decode_dc(&img,  &img.dc_huff_tables[y_dc_table_id], &dc_pred_Y);
		decode_ac(&img, &img.ac_huff_tables[y_ac_table_id], Y_block);
		
		int dequantized[8][8];
		dequantizer(Y_block, img.quant_tables[y_qt_id] ,dequantized);
		float idct_out [8][8];
		idct(dequantized, idct_out);
		
		int x0 = mcu_x * 16 + (b % 2) * 8;
                int y0 = mcu_y * 16 + (b / 2) * 8;
                for (int by = 0; by < 8; by++) {
                    for (int bx = 0; bx < 8; bx++) {
                        int px = x0 + bx;
                        int py = y0 + by;
                        if (px < width && py < height) {
                            float val = (idct_out[by][bx] + 128.0);
                            if (val < 0) val = 0;
                            if (val > 255) val = 255;
                            Y_PLANE[py * width + px] = (unsigned char)val;
                        }
                    }
                }	
	}
	
	int cb_dc_table_id= img.components[1].dc_table_id;
	int cb_ac_table_id= img.components[1].ac_table_id;
	int block_no_cb= mcu_y*img.mcus_across + mcu_x;
	int *cb_block=  &img.dct_coeffs[1][block_no_cb * 64];
	
	cb_block[0]= decode_dc(&img,  &img.dc_huff_tables[cb_dc_table_id], &dc_pred_Cb);
		decode_ac(&img, &img.ac_huff_tables[cb_ac_table_id], cb_block);
	
	int dequantized_cb[8][8];
		dequantizer(cb_block,   img.quant_tables[cb_qt_id], dequantized_cb);
		float idct_out_cb [8][8];
		idct(dequantized_cb, idct_out_cb);
	
	     // Upsample Cb from 8x8 to 16x16 (replicate each pixel 2x2)
            for (int by = 0; by < 8; by++) {
                for (int bx = 0; bx < 8; bx++) {
                    float val = (idct_out_cb[by][bx] + 128.0);
                    if (val < 0) val = 0;
                    if (val > 255) val = 255;
                    unsigned char sample = (unsigned char)val;
                    int px0 = mcu_x * 16 + bx * 2;
                    int py0 = mcu_y * 16 + by * 2;
                    for (int dy = 0; dy < 2; dy++) {
                        for (int dx = 0; dx < 2; dx++) {
                            int px = px0 + dx;
                            int py = py0 + dy;
                            if (px < width && py < height) {
                                CB_PLANE[py * width + px] = sample;
                            }
                        }
                    }
                }
            }
	
	
	
	int cr_dc_table_id= img.components[2].dc_table_id;
	int cr_ac_table_id= img.components[2].ac_table_id;
	int block_no_cr= mcu_y*img.mcus_across + mcu_x;
	int *cr_block=  &img.dct_coeffs[2][block_no_cr * 64];
	
	cr_block[0]= decode_dc(&img,  &img.dc_huff_tables[cr_dc_table_id], &dc_pred_Cr);
		decode_ac(&img, &img.ac_huff_tables[cr_ac_table_id], cr_block);
	
	int dequantized_cr[8][8];
		dequantizer(cr_block, img.quant_tables[cr_qt_id], dequantized_cr);
		float idct_out_cr [8][8];
		idct(dequantized_cr, idct_out_cr);
	
	    
            for (int by = 0; by < 8; by++) {
                for (int bx = 0; bx < 8; bx++) {
                    float val = (idct_out_cr[by][bx] + 128.0);
                    if (val < 0) val = 0;
                    if (val > 255) val = 255;
                    unsigned char sample = (unsigned char)val;
                    int px0 = mcu_x * 16 + bx * 2;
                    int py0 = mcu_y * 16 + by * 2;
                    for (int dy = 0; dy < 2; dy++) {
                        for (int dx = 0; dx < 2; dx++) {
                            int px = px0 + dx;
                            int py = py0 + dy;
                            if (px < width && py < height) {
                                CR_PLANE[py * width + px] = sample;
                            }
                        }
                    }
                }
            }
	
}
	
}

  // Convert to BGR and write BMP
    unsigned char *bmp_data = (unsigned char*)malloc(width * height * 3 * sizeof(unsigned char));
    if (!bmp_data) {
        printf("BMP data allocation failed\n");
            free(Y_PLANE);
    free(CB_PLANE);
    free(CR_PLANE);
        fclose(fptr);
        free(buffer);
        return EXIT_FAILURE;
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            ycbcr_to_rgb(Y_PLANE[idx], CB_PLANE[idx], CR_PLANE[idx], &bmp_data[(y * width + x) * 3]);
        }
    }

    write_bmp("output.bmp", width, height, bmp_data);

    // Open the image with default viewer
#ifdef _WIN32
    system("start output.bmp");
#else
    system("xdg-open output.bmp 2>/dev/null || open output.bmp 2>/dev/null");
#endif

    free(Y_PLANE);
    free(CB_PLANE);
    free(CR_PLANE);
    free(bmp_data);



fclose(fptr);
printf("\n");
printf(" file opened then closed successfully");
free(buffer);
	
return 0;	
}

