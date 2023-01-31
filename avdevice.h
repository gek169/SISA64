

#include "sisa64.h"
#include <SDL2/SDL.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

#define AUDIO_MEM_SZ 0x100000
#define VIDEO_MEM_SZ (SCREEN_WIDTH * SCREEN_HEIGHT * 4 * 2)

static uint8_t audiomemory[AUDIO_MEM_SZ] = {0};
static uint8_t vmem[VIDEO_MEM_SZ] = {0}; /*two screens worth of video memory.*/
static int32_t audio_left = 0;

static SDL_Window *sdl_win = NULL;
static SDL_Renderer *sdl_rend = NULL;
static SDL_Texture *sdl_tex = NULL;
static SDL_AudioSpec sdl_spec = {0};
static const int32_t display_scale = 1;
static uint8_t shouldquit = 0;

static uint64_t video_register_A;
static uint64_t video_register_B;
static uint64_t video_register_C;
static uint64_t video_register_D;



static void pollevents(){
	SDL_Event ev;
	while(SDL_PollEvent(&ev)){
		if(ev.type == SDL_QUIT) shouldquit = 1; /*Magic value for quit.*/

		/*
		else if(ev.type == SDL_TEXTINPUT){
			char* b = ev.text.text;
			while(*b) {
				stdin_buf_push_char(*b); b++;
			}
		}else if(ev.type == SDL_KEYDOWN){
			switch(ev.key.keysym.scancode){
				default: break;
				case SDL_SCANCODE_DELETE: stdin_buf_push_char(0x7F); break;
				case SDL_SCANCODE_BACKSPACE: stdin_buf_push_char(0x7F);break;
				case SDL_SCANCODE_KP_BACKSPACE: stdin_buf_push_char(0x7F);break;
				case SDL_SCANCODE_RETURN: stdin_buf_push_char(0xa);break;
				case SDL_SCANCODE_RETURN2: stdin_buf_push_char(0xa);break;
				case SDL_SCANCODE_KP_ENTER: stdin_buf_push_char(0xa);break;
				case SDL_SCANCODE_ESCAPE: stdin_buf_push_char('\e');break;
			}
		}
		*/
	}
}



static void audio_mem_write(uint64_t addr, uint8_t val){
	audiomemory[addr] = val;
}
static void vmem_write(uint64_t addr, uint8_t val){
	vmem[addr] = val;
}
static uint8_t vmem_read(uint64_t addr){
	return vmem[addr];
}
static void haltaudio(){audio_left = 0;}
static void playaudio(uint32_t sz){
	audio_left = sz;
	if(sz >= AUDIO_MEM_SZ) audio_left = AUDIO_MEM_SZ-1;
}
static uint64_t read_gamer_buttons(){
	uint64_t retval = 0;
	const unsigned char *state;
	SDL_PumpEvents();
	state = SDL_GetKeyboardState(NULL);
	retval |= 0x1 * (state[SDL_SCANCODE_UP]!=0);
	retval |= 0x2 * (state[SDL_SCANCODE_DOWN]!=0);
	retval |= 0x4 * (state[SDL_SCANCODE_LEFT]!=0);
	retval |= 0x8 * (state[SDL_SCANCODE_RIGHT]!=0);

	retval |= 0x10 * (state[SDL_SCANCODE_RETURN]!=0);
	retval |= 0x20 * (state[SDL_SCANCODE_RSHIFT]!=0);
	retval |= 0x40 * (state[SDL_SCANCODE_Z]!=0);
	retval |= 0x80 * (state[SDL_SCANCODE_X]!=0);
	retval |= 0x100 * (state[SDL_SCANCODE_C]!=0);
	
	retval |= 0x200 * (state[SDL_SCANCODE_A]!=0);
	retval |= 0x400 * (state[SDL_SCANCODE_S]!=0);
	retval |= 0x800 * (state[SDL_SCANCODE_D]!=0);
	return retval;
}

static void renderscreen(){
		SDL_Rect screenrect;
		SDL_Rect screenrect2;
		screenrect.x = 0;
		screenrect.y = 0;
		screenrect.w = SCREEN_WIDTH;
		screenrect.h = SCREEN_HEIGHT;
		screenrect2 = screenrect;
		screenrect2.w *= display_scale;
		screenrect2.h *= display_scale;
		SDL_UpdateTexture(
			sdl_tex,
			NULL,
			vmem,
			(SCREEN_WIDTH) * 4
		);
		SDL_RenderCopy(
			sdl_rend, 
			sdl_tex,
			&screenrect,
			&screenrect2
		);
		SDL_RenderPresent(sdl_rend);
		return;
}


static void sdl_audio_callback(void *udata, Uint8 *stream, int len){
	(void)udata;
	SDL_memset(stream, 0, len);
	if(audio_left == 0){return;}
	if(len >= audio_left){
		len = audio_left;
	}
	/*len = (len < audio_left) ? len : audio_left;*/
	
	SDL_MixAudio(stream, audiomemory + (AUDIO_MEM_SZ - audio_left), len, SDL_MIX_MAXVOLUME);
	audio_left -= len;
}


static void av_init(){
		SDL_DisplayMode DM;
	    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
	    {
	        printf("SDL2 could not be initialized!\n"
	               "SDL_Error: %s\n", SDL_GetError());
	        exit(1);
	    }
	    SDL_GetCurrentDisplayMode(0, &DM);

	    /*if(DM.w > 1000) display_scale = 2;*/
		sdl_spec.freq = 16000;
		sdl_spec.format = AUDIO_S16MSB;
		sdl_spec.channels = 1;
		sdl_spec.silence = 0;
		sdl_spec.samples = 2048;
		sdl_spec.callback = sdl_audio_callback;
		sdl_spec.userdata = NULL;
		sdl_win = SDL_CreateWindow("[SISA-64]",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			SCREEN_WIDTH * display_scale, 
			SCREEN_HEIGHT * display_scale,
			SDL_WINDOW_SHOWN
		);
		if(!sdl_win)
		{
			printf("SDL2 window creation failed.\n"
				"SDL_Error: %s\n", SDL_GetError());
			exit(1);
		}
		sdl_rend = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_ACCELERATED);
		if(!sdl_rend){
			printf("SDL2 renderer creation failed.\n"
				"SDL_Error: %s\n", SDL_GetError());
			exit(1);
		}
		sdl_tex = SDL_CreateTexture(sdl_rend, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 
			SCREEN_WIDTH,
			SCREEN_HEIGHT
		);
		if(!sdl_tex){
			printf("SDL2 texture creation failed.\n"
				"SDL_Error: %s\n", SDL_GetError());
			exit(1);
		}
		if ( SDL_OpenAudio(&sdl_spec, NULL) < 0 ){
		  printf("\r\nSDL2 audio opening failed!\r\n"
		  "SDL_Error: %s\r\n", SDL_GetError());
		  exit(-1);
		}
		SDL_PauseAudio(0);
		SDL_StartTextInput();
}


static uint8_t pv(){ /*Poll events.*/
	pollevents();
	return shouldquit;
}


#define BEGIN_CONTROLLER 	0x200000000
#define BEGIN_VMEM 			0x300000000
#define BEGIN_AMEM 			0x400000000


static uint64_t av_device_read(uint64_t addr){
	const uint64_t loc_vstring = 0x400;
	const char* vendor_string = "S-ISA-64 Emulator: SDL2 Audio-Video Controller.";
	if(addr == 0) return 0x1ABC;
	if(addr == 1) return 1;
	if(addr == 2) return loc_vstring;
	/*cannot be disabled*/
	if(addr == 3) return 0;
	/*not thread safe!*/
	if(addr == 4) return 0;
	/*Device implemented.*/
	if( (addr >= loc_vstring) &&
		((addr - 0x400) < strlen(vendor_string))
	) return vendor_string[addr - 0x400];

	/*active read addresses.*/
	if(addr == (BEGIN_CONTROLLER+3)) {return video_register_A;}
	if(addr == (BEGIN_CONTROLLER+4)) {return video_register_B;}
	if(addr == (BEGIN_CONTROLLER+5)) {return video_register_C;}
	if(addr == (BEGIN_CONTROLLER+6)) {return video_register_D;}
	if(addr == (BEGIN_CONTROLLER + 100)) return pv();
	if(addr == (BEGIN_CONTROLLER + 101)) read_gamer_buttons();
	if(addr >= BEGIN_VMEM && addr < (BEGIN_VMEM + VIDEO_MEM_SZ))
		return vmem_read(addr - BEGIN_VMEM);

	return 0;
}

static void av_device_write(uint64_t addr, uint64_t val){
	/*TODO*/
	if(addr == BEGIN_CONTROLLER) {playaudio(val);return;}
	if(addr == (BEGIN_CONTROLLER+1)) {haltaudio();return;}
	if(addr == (BEGIN_CONTROLLER+2)) {renderscreen();return;}
	/*address the registers.*/
	if(addr == (BEGIN_CONTROLLER+3)) {video_register_A = val; return;}
	if(addr == (BEGIN_CONTROLLER+4)) {video_register_B = val; return;}
	if(addr == (BEGIN_CONTROLLER+5)) {video_register_C = val; return;}
	if(addr == (BEGIN_CONTROLLER+6)) {video_register_D = val; return;}
	/*Hardware DMA, ram to vmem*/
	if(addr == (BEGIN_CONTROLLER+7)) {
		memcpy(
			vmem + video_register_A,
			sisa64_mem + video_register_B,
			video_register_C
		);
	}
	/*Hardware DMA, ram to ram*/
	if(addr == (BEGIN_CONTROLLER+8)) {
		memcpy(
			sisa64_mem + video_register_A,
			sisa64_mem + video_register_B,
			video_register_C
		);
	}	
	/*Hardware DMA, vmem to vmem*/
	if(addr == (BEGIN_CONTROLLER+9)) {
		memcpy(
			vmem + video_register_A,
			vmem + video_register_B,
			video_register_C
		);
	}
	/*Hardware DMA, vmem to ram*/
	if(addr == (BEGIN_CONTROLLER+10)) {
		memcpy(
			sisa64_mem + video_register_A,
			vmem + video_register_B,
			video_register_C
		);
	}
	
	if(addr >= BEGIN_VMEM && addr < (BEGIN_VMEM + VIDEO_MEM_SZ)){
		vmem_write(addr - BEGIN_VMEM, val);
		return;
	}
	if(addr >= BEGIN_AMEM && addr < (BEGIN_AMEM + AUDIO_MEM_SZ)){
		audio_mem_write(addr - BEGIN_AMEM, val);
		return;
	}
}
