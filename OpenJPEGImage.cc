#define DEBUG 0

#include "OpenJPEGImage.h"
#include <sstream>
#include <math.h>
#include <fstream>	
#include <Timer.h>

using namespace std;

#ifdef DEBUG
	extern ofstream logfile;
#endif


/************************************************************************/
/*                            callbacks                                 */
/************************************************************************/

static void error_callback(const char *msg, void *client_data){
	stringstream ss;
	ss << "ERROR :: OpenJPEG core :: " << msg;
	throw string(ss.str());
}

static void warning_callback(const char *msg, void *client_data){
	#ifdef DEBUG
		logfile << "WARNING :: OpenJPEG core :: " << msg; logfile.flush();
	#endif
}

static void info_callback(const char *msg, void *client_data){
	#ifdef DEBUG
		logfile << "INFO :: OpenJPEG core :: " << msg; logfile.flush();
	#endif
}

/************************************************************************/
/*                            openImage()                               */
/************************************************************************/

void OpenJPEGImage::openImage() throw (string){

	#ifdef DEBUG
		Timer timer; timer.start();
		logfile << "INFO :: OpenJPEG :: openImage() :: started" << endl; logfile.flush();
	#endif
	
	string filename = getFileName( currentX, currentY ); // Get file name
 	updateTimestamp(filename); // Check if our image has been modified

	if(!(fsrc = fopen(filename.c_str(), "rb"))) throw string("ERROR :: OpenJPEG :: openImage() :: failed to open file for reading");	

	loadImageInfo(currentX, currentY);
	isSet = true;
    
	#ifdef DEBUG
		logfile << "INFO :: OpenJPEG :: openImage() :: " << timer.getTime() << " microseconds" << endl; logfile.flush();
	#endif
}


/************************************************************************/
/*                            closeImage()                              */
/************************************************************************/

void OpenJPEGImage::closeImage(){

	#ifdef DEBUG
		logfile << "INFO :: OpenJPEG :: closeImage() :: started" << endl; logfile.flush();
	#endif
	
	if(fsrc){
		fclose(fsrc);
		fsrc = NULL;
	}

	#ifdef DEBUG
		logfile << "INFO :: OpenJPEG :: closeImage() :: ended" << endl; logfile.flush();
	#endif
}

/************************************************************************/
/*                            loadImageInfo()                           */
/************************************************************************/

void OpenJPEGImage::loadImageInfo( int seq, int ang ) throw(string){

	#ifdef DEBUG
		Timer timer; timer.start();
		logfile << "INFO :: OpenJPEG :: loadImageInfo() :: started" << endl; logfile.flush();
	#endif

	opj_image_t* l_image; // Image structure
	opj_stream_t* l_stream;	// File stream
	opj_codec_t* l_codec; // Handle to a decompressor
	
	try{	
		l_codec = opj_create_decompress(OPJ_CODEC_JP2); // Create decompress codec
		
		opj_set_info_handler(l_codec, info_callback, 00); // Set callback handlers 
		opj_set_warning_handler(l_codec, warning_callback, 00);
		opj_set_error_handler(l_codec, error_callback, 00);
		
		opj_dparameters_t parameters; // Set default decoder parameters
		opj_set_default_decoder_parameters(&parameters);
		if(!opj_setup_decoder(l_codec, &parameters)) 
			throw string("ERROR :: OpenJPEG :: openImage() :: opj_setup_decoder() failed"); // Setup decoder
		
		if(!(l_stream = opj_stream_create_default_file_stream(fsrc,1)))
			throw string("ERROR :: OpenJPEG :: openImage() :: opj_stream_create_default_file_stream() failed"); // Create stream

		if(!opj_read_header(l_stream, l_codec, &l_image)) throw string("ERROR :: OpenJPEG :: openImage() :: opj_read_header() failed"); // Read main header	

		opj_codestream_info_v2_t* cst_info = opj_get_cstr_info(l_codec); // Get info structure
		image_tile_width = cst_info->tdx;
		image_tile_height = cst_info->tdy;
		numResolutions = cst_info->m_default_tile_info.tccp_info[0].numresolutions;
		max_layers = cst_info->m_default_tile_info.numlayers;
		#ifdef DEBUG
			logfile << "OpenJPEG :: " << max_layers << " quality layers detected" << endl; logfile.flush();
		#endif
		opj_destroy_cstr_info(&cst_info);
		
		if(	l_image->x1 <= l_image->x0 || l_image->y1 <= l_image->y0 || l_image->numcomps == 0 ||
			l_image->comps[0].w != l_image->x1 - l_image->x0 || l_image->comps[0].h != l_image->y1 - l_image->y0)
				throw string("ERROR :: OpenJPEG :: loadImageInfo() :: Could not handle that image");

		if(	l_image->color_space != OPJ_CLRSPC_SRGB && l_image->numcomps == 3 &&
			l_image->comps[1].w == l_image->comps[0].w / 2 && l_image->comps[1].h == l_image->comps[0].h / 2 &&
			l_image->comps[2].w == l_image->comps[0].w / 2 && l_image->comps[2].h == l_image->comps[0].h / 2)
				throw string("ERROR :: OpenJPEG :: loadImageInfo() :: 420 format detected.");
		else 
			for(unsigned short i = 2; i <= (int)l_image->numcomps; ++i)
			    if(l_image->comps[i-1].w != l_image->comps[0].w || l_image->comps[i-1].h != l_image->comps[0].h)
			    	throw string("ERROR :: OpenJPEG :: loadImageInfo() :: Could not handle that image");
		
		switch((channels = l_image->numcomps)){
			case 3: colourspace = GREYSCALE; break;
			case 1: colourspace = GREYSCALE; break;
			case 8: colourspace = CIELAB; break;
			default: throw string("ERROR :: OpenJPEG :: Unsupported color space");
		}
		
		bpp = l_image->comps[0].bpp; // Save bit depth
		sgnd = l_image->comps[0].sgnd;
		
		image_widths.push_back((raster_width = l_image->x1 - l_image->x0)); // Save first resolution level
		image_heights.push_back((raster_height = l_image->y1 - l_image->y0));

		int tmp_w = raster_width;
		int tmp_h = raster_height;
		#ifdef DEBUG 
			logfile << "INFO :: OpenJPEG :: Resolution : " << tmp_w << "x" << tmp_h << endl; logfile.flush();
		#endif
		unsigned short i = 1;
		for(; tmp_w > tile_width || tmp_h > tile_height; ++i){
			image_widths.push_back((tmp_w /= 2));
			image_heights.push_back((tmp_h /= 2));
			#ifdef DEBUG
				logfile << "INFO :: OpenJPEG :: Resolution : " << tmp_w << "x" << tmp_h << endl; logfile.flush();
			#endif
		}
		
		#ifdef DEBUG
	  		if(i > numResolutions){
				logfile << 	"WARNING :: OpenJPEG :: Insufficient resolution levels in JPEG2000 stream. Will generate " << 
							i - numResolutions << " extra levels dynamically." << endl;
	  		}
  		#endif
  		
  		logfile << "INFO :: OpenJPEG :: I :: " << i << endl; logfile.flush();
  		
  		if(i > numResolutions) virtual_levels = i-numResolutions;
  		numResolutions = i;
  		
  		logfile << "INFO :: OpenJPEG :: virtual_levels :: " << virtual_levels << endl; logfile.flush();
	}
	catch(...){
		if(l_codec && l_stream) opj_end_decompress (l_codec, l_stream);
		if(l_stream) opj_stream_destroy(l_stream);
		if(l_codec) opj_destroy_codec(l_codec);
		if(l_image) opj_image_destroy(l_image);	
		throw;	
	}
	opj_end_decompress (l_codec, l_stream);
	opj_stream_destroy(l_stream);
	opj_destroy_codec(l_codec);
	opj_image_destroy(l_image);	
	
	#ifdef DEBUG
		logfile << "INFO :: OpenJPEG :: loadImageInfo() :: " << timer.getTime() << " microseconds" << endl; logfile.flush();
	#endif
}

/************************************************************************/
/*                  getTile() - Get an individual tile                  */
/************************************************************************/

RawTile OpenJPEGImage::getTile(int seq, int ang, unsigned int res, int layers, unsigned int tile) throw (string){

	#ifdef DEBUG
		Timer timer; timer.start();
		logfile << "INFO :: OpenJPEG :: getTile() :: started" << endl; logfile.flush();
	#endif

	if(res > numResolutions)
		throw string("ERROR :: OpenJPEG :: getTile() :: Asked for non-existant resolution"); // Check whether requested resolution exists in the picture

	int vipsres = ( numResolutions - 1 ) - res;

	unsigned int rem_x = image_widths[vipsres] % tile_width; // Get the width and height for last row and column tiles
	unsigned int rem_y = image_heights[vipsres] % tile_height; // that means remaining pixels, which do not form a whole tile

	unsigned int ntlx = (image_widths[vipsres] / tile_width) + (rem_x == 0 ? 0 : 1); // Calculate the number of tiles in each direction
	unsigned int ntly = (image_heights[vipsres] / tile_height) + (rem_y == 0 ? 0 : 1);

	#ifdef DEBUG
		logfile <<	"INFO :: OpenJPEG :: getTile() :: asked for: " << endl <<
					"\tTile width: " << tile_width << endl <<
					"\tTile height: " << tile_height << endl <<
					"\tResolution: " << res << ", meaning " << vipsres << " for OpenJPEG driver" << endl <<
					"\tResolution: " << image_widths[vipsres] << "x" << image_heights[vipsres] << endl <<
					"\tTile index: " << tile << endl <<
					"\tTiles available: " << ntlx << "x" << ntly << endl <<
					"\tRemaining tile width in last column: " << rem_x << endl <<
					"\tRemaining tile height in bottom row: " << rem_y << endl; logfile.flush();
	#endif

	if(tile >= ntlx*ntly) throw string("ERROR :: OpenJPEG :: getTile() :: Asked for non-existant tile"); // Check whether requested tile exists

	unsigned int tw = tile_width, th = tile_height;
	
	bool edge_x = false; // Alter the tile size if it's in the last column
	if((tile % ntlx == ntlx - 1) && rem_x != 0){
    	tw = rem_x;
    	edge_x = true;
	}
  
	bool edge_y = false; // Alter the tile size if it's in the bottom row
	if((tile / ntlx == ntly - 1) && rem_y != 0){
    	th = rem_y;
    	edge_y = true;
	}

	int xoffset = (tile % ntlx) * tile_width; // Calculate the pixel offsets for this tile
  	int yoffset = (tile / ntlx) * tile_height;

	#ifdef DEBUG
		logfile << "\tFinal tile size requested: " << tw << "x" << th << " @" << channels << endl; logfile.flush();
	#endif

  	RawTile rawtile(tile, res, seq, ang, tw, th, channels, 8); // Create Rawtile object and initialize with data
	rawtile.data = new unsigned char[tw*th*channels];
	rawtile.dataLength = tw*th*channels;
	rawtile.filename = getImagePath();
	rawtile.timestamp = timestamp;

	// Set the number of layers to half of the number of detected layers if we have not set the layers parameter manually 
  	if(layers <= 0) layers = ceil(max_layers/2.0);
	else if(layers < 1) layers = 1;
	
	// Process the tile - save da	ta to rawfile.data
	process(tw, th, xoffset, yoffset, res, layers, (image_tile_width == tw && image_tile_height == th) ? tile : -1, rawtile.data);

	#ifdef DEBUG
		logfile << "INFO :: OpenJPEG :: getTile() :: " << timer.getTime() << " microseconds" << endl; logfile.flush();
	#endif

	return rawtile;
}

/************************************************************************/
/*         getRegion() - Get an entire region and not just a tile         */
/************************************************************************/

void OpenJPEGImage::getRegion(int seq, int ang, unsigned int res, int layers, int x, int y, unsigned int w, unsigned int h, unsigned char* buf) throw (string){

	#ifdef DEBUG
		Timer timer; timer.start();
		logfile << "INFO :: OpenJPEG :: getRegion() :: started" << endl; logfile.flush();
	#endif
	
	if(res > numResolutions) throw string("ERROR :: OpenJPEG :: getRegion() :: Asked for non-existant resolution");
	
	if(layers <= 0) layers = ceil(max_layers/2.0);
	else if(layers < 1) layers = 1;
	
	int vipsres = (numResolutions - 1) - res;
	
	if((x + w) > image_widths[vipsres] || (y + h) > image_heights[vipsres]) throw string("ERROR :: OpenJPEG :: getRegion() :: Asked for region out of raster size");
	
	process(w, h, x, y, res, layers, -1, buf);

	#ifdef DEBUG
		logfile << "INFO :: OpenJPEG :: getRegion() :: " << timer.getTime() << " microseconds" << endl; logfile.flush();
	#endif
	
}

/************************************************************************/
/*        		 process() - Main processing function   	    		*/
/************************************************************************/

void OpenJPEGImage::process(unsigned int tw, unsigned int th, unsigned int xoffset, unsigned int yoffset, unsigned int res, int layers, int tile, void *d) throw (string){

    opj_image_t* out_image;
    int vipsres = (numResolutions - 1) - res;
    opj_stream_t* l_stream;	// File stream
	opj_codec_t* l_codec; // Handle to a decompressor
	unsigned int factor = 1; // Downsampling factor
	
	if(res < virtual_levels){ // Handle virtual resolutions
		factor = 2 * (virtual_levels - res);
		xoffset = 0;
		yoffset = 0;
		tw *= factor;
		th *= factor;
		vipsres = numResolutions - 1 - virtual_levels;
	}
	
	try{
		l_codec = opj_create_decompress(OPJ_CODEC_JP2); // Create decompress codec
		opj_set_info_handler(l_codec, info_callback, 00); // Set callback handlers 
		opj_set_warning_handler(l_codec, warning_callback, 00);
		opj_set_error_handler(l_codec, error_callback, 00);
		
		if(!(l_stream = opj_stream_create_default_file_stream(fsrc, 1)))
			throw string("ERROR :: OpenJPEG :: process() :: opj_stream_create_default_file_stream() failed"); // Create stream
			
		opj_dparameters_t params; 
		params.cp_layer = layers;
		params.cp_reduce = 0;
		if(!opj_setup_decoder(l_codec, &params)) throw string("ERROR :: OpenJPEG :: process() :: opj_setup_decoder() failed"); // Setup layers
		
		if(!opj_read_header(l_stream, l_codec, &out_image)) throw string("ERROR :: OpenJPEG :: process() :: opj_read_header() failed"); // Read main header
		if(!opj_set_decoded_resolution_factor(l_codec, vipsres)) 
			throw string("ERROR :: OpenJPEG :: process() :: opj_set_decoded_resolution_factor() failed"); // Setup resolution
		#ifdef DEBUG
			Timer timer; timer.start();
			logfile << "INFO :: OpenJPEG :: process() :: Decoding started" << endl; logfile.flush();
		#endif
		if(tile < 0){
			#ifdef DEBUG
				logfile << "INFO :: OpenJPEG :: process() :: Decoding a region (not a single tile)" << endl; logfile.flush();
			#endif
			if(!opj_set_decode_area(l_codec, out_image, xoffset, yoffset, xoffset+tw, yoffset+th)) 
				throw string("ERROR :: OpenJPEG :: process() :: opj_set_decode_area() failed");
			if(!opj_decode(l_codec, l_stream, out_image)) throw string("ERROR :: OpenJPEG :: process() :: opj_decode() failed");
		}
		else if(!opj_get_decoded_tile(l_codec, l_stream, out_image, tile)) throw string("ERROR :: OpenJPEG :: process() :: opj_get_decoded_tile() failed");
		
		#ifdef DEBUG
			logfile <<	"INFO :: OpenJPEG :: process() :: Decoding took " << timer.getTime() << " microseconds" << endl <<
						"INFO :: OpenJPEG :: process() :: Decoded image info: " << endl <<
						"\tPrecision: " << out_image->comps[0].prec << endl <<
						"\tBPP: " << out_image->comps[0].bpp << endl <<
						"\tSigned: " << out_image->comps[0].sgnd << endl <<
						"\tXOFF: " << out_image->comps[0].x0 << endl <<
						"\tYOFF: " << out_image->comps[0].y0 << endl <<
						"\tXSIZE: " << out_image->comps[0].w << endl <<
						"\tYSIZE: " << out_image->comps[0].h << endl <<
						"\tRESNO: " << out_image->comps[0].resno_decoded << endl <<
						"INFO :: OpenJPEG :: process() :: Copying image data started" << endl; logfile.flush();
			timer.start();
		#endif
		
		unsigned char* p_buffer = (unsigned char*) d;
		unsigned int buffer_write_pos = 0;
		unsigned int h_pos = 0; unsigned int w_pos; unsigned int xy_position = 0;
		unsigned int color_comp;
		for(; h_pos < th; h_pos += factor){
			for(w_pos = 0; w_pos < tw; w_pos += factor){
				xy_position = (tw*h_pos) + w_pos;
				for(color_comp = 0; color_comp < channels; ++color_comp){ // For each color component in that pixel
				    OPJ_INT32 int_data = out_image->comps[color_comp].data[xy_position]; 

					p_buffer[buffer_write_pos+color_comp] = int_data & 0x000000ff;
					//p_buffer[xy_position+(color_comp*4)+1] = (int_data & 0x0000ff00) >> 8;
					//p_buffer[xy_position+(color_comp*4)+2] = (int_data & 0x00ff0000) >> 16;
					//p_buffer[xy_position+(color_comp*4)+3] = (int_data & 0xff000000) >> 24;
					// what is in the other 3 bytes??
				}
				buffer_write_pos += channels;
			}
		}
		
		#ifdef DEBUG
			logfile << "INFO :: OpenJPEG :: process() :: Copying image data took " << timer.getTime() << " microseconds" << endl; logfile.flush();
		#endif
	}
	catch(...){
		opj_end_decompress (l_codec, l_stream);
		opj_stream_destroy(l_stream);
		opj_destroy_codec(l_codec);
		opj_image_destroy(out_image);
		throw;
	}
	opj_end_decompress (l_codec, l_stream);
	opj_stream_destroy(l_stream);
	opj_destroy_codec(l_codec);
	opj_image_destroy(out_image);	
}

