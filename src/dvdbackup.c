/*
 * dvdbackup - tool to rip DVDs from the command line
 *
 * Copyright (C) 2002  Olaf Beck <olaf_sc@yahoo.com>
 * Copyright (C) 2008-2012  Benjamin Drung <benjamin.drung@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

/* C standard libraries */
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* C POSIX library */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* libdvdread */
#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>

#include "dvdread_internal.h"
#define PRIV(a) container_of(a, struct ifo_handle_private_s, handle)

/* internationalisation */
#include "gettext.h"
#define _(String) gettext(String)

#include "dvdbackup.h"
#include "dvdlogger.h"

#ifdef FIND_UNUSED
#include "find-sector.h"
#endif

#define MAXNAME 256

/**
 * Buffer size in DVD logical blocks (2 KiB).
 * Currently set to 1 MiB.
 */
#define BUFFER_SIZE 512

/**
 * The maximum size of a VOB file is 1 GiB or 524288 in Video DVD logical block
 * respectively.
 */
#define MAX_VOB_SIZE 524288

#define DVD_SEC_SIZ 2048

/* Flag for verbose mode */
int verbose = 0;
int aspect;
int progress = 0;
char progressText[MAXNAME] = "n/a";

/* Structs to keep title set information in */

typedef struct {
	off_t size_ifo;
	off_t size_menu;
	int number_of_vob_files;
	off_t size_vob[10];
} title_set_t;

typedef struct {
	int number_of_title_sets;
	title_set_t* title_set;
} title_set_info_t;


typedef struct {
	int title;
	int title_set;
	int vts_title;
	int chapters;
	int aspect_ratio;
	int angles;
	int audio_tracks;
	int audio_channels;
	int sub_pictures;
} titles_t;

typedef struct {
	int main_title_set;
	int number_of_titles;
	titles_t* titles;
} titles_info_t;


static void bsort_max_to_min(int sector[], int title[], int size);


static int CheckSizeArray(const int size_array[], int reference, int target) {
	if(size_array[target] && (size_array[reference]/size_array[target] == 1) &&
			((size_array[reference] * 2 - size_array[target])/ size_array[target] == 1) &&
			((size_array[reference]%size_array[target] * 3) < size_array[reference]) ) {
		/* We have a dual DVD with two feature films - now let's see if they have the same amount of chapters*/
		return(1);
	} else {
		return(0);
	}
}


static int CheckAudioSubChannels(int audio_audio_array[], int title_set_audio_array[],
		int subpicture_sub_array[], int title_set_sub_array[],
		int channels_channel_array[],int title_set_channel_array[],
		int reference, int candidate, int title_sets) {

	int temp, i, found_audio, found_sub, found_channels;

	found_audio=0;
	temp = audio_audio_array[reference];
	for (i=0 ; i < title_sets ; i++ ) {
		if ( audio_audio_array[i] < temp ) {
			break;
		}
		if ( candidate == title_set_audio_array[i] ) {
			found_audio=1;
			break;
		}

	}

	found_sub=0;
	temp = subpicture_sub_array[reference];
	for (i=0 ; i < title_sets ; i++ ) {
		if ( subpicture_sub_array[i] < temp ) {
			break;
		}
		if ( candidate == title_set_sub_array[i] ) {
			found_sub=1;
			break;
		}

	}


	found_channels=0;
	temp = channels_channel_array[reference];
	for (i=0 ; i < title_sets ; i++ ) {
		if ( channels_channel_array[i] < temp ) {
			break;
		}
		if ( candidate == title_set_channel_array[i] ) {
			found_channels=1;
			break;
		}

	}


	return(found_audio + found_sub + found_channels);
}




static int DVDWriteCells(dvd_reader_t * dvd, int cell_start_sector[],
		int cell_end_sector[], int length, int titles,
		title_set_info_t * title_set_info, titles_info_t * titles_info,
		char * targetdir,char * title_name) {

	/* Loop variables */
	int i;

	/* Vob control */
	int vob = 1;

	/* Temp filename,dirname */
	char *targetname;
	size_t targetname_length;

	/* Write buffer */

	unsigned char * buffer=NULL;

	/* File Handler */
	int streamout;

	int size;
	int left;

	int to_read;
	int have_read;

	/* Offsets */
	int soffset;


	/* DVD handler */
	dvd_file_t* dvd_file = NULL;

	int title_set;

#ifdef DEBUG
	int number_of_vob_files;

	XLog4(pApp, "DVDWriteCells: length is %d", length);
#endif


	title_set = titles_info->titles[titles - 1].title_set;
#ifdef DEBUG
	number_of_vob_files = title_set_info->title_set[title_set].number_of_vob_files;
	XLog4(pApp, "DVDWriteCells: title set is %d", title_set);
	XLog4(pApp, "DVDWriteCells: vob files are %d", number_of_vob_files);
#endif

	// Reserve space for "<targetdir>/<title_name>/VIDEO_TS/VTS_XX_X.VOB" and terminating "\0"
	targetname_length = strlen(targetdir) + strlen(title_name) + 24;
	targetname = malloc(targetname_length);
	if (targetname == NULL) {
		XLog0(pApp, _("Failed to allocate %zu bytes for a filename."), targetname_length);
		return 1;
	}

	/* Remove all old files silently if they exists */

	for ( i = 0 ; i < 10 ; i++ ) {
		snprintf(targetname, targetname_length, "%s/%s/VIDEO_TS/VTS_%02i_%i.VOB", targetdir, title_name, title_set, i + 1);
#ifdef DEBUG
		XLog4(pApp, "DVDWriteCells: file is %s", targetname);
#endif
		unlink( targetname);
	}


#ifdef DEBUG
	for (i = 0; i < number_of_vob_files ; i++) {
		XLog4(pApp, "vob %i size: %lld", i + 1, title_set_info->title_set[title_set].size_vob[i]);
	}
#endif

	/* Create VTS_XX_X.VOB */
	if (title_set == 0) {
		XLog2(pApp,_("Do not try to copy chapters from the VMG domain; there are none."));
		free(targetname);
		return(1);
	} else {
		snprintf(targetname, targetname_length, "%s/%s/VIDEO_TS/VTS_%02i_%i.VOB", targetdir, title_name, title_set, vob);
	}

#ifdef DEBUG
	XLog4(pApp, "DVDWriteCells: 1");
#endif

	if ((buffer = (unsigned char *)malloc(BUFFER_SIZE * DVD_VIDEO_LB_LEN * sizeof(unsigned char))) == NULL) {
		XLog0(pApp, _("Out of memory copying %s"), targetname);
		free(targetname);
		return(1);
	}

#ifdef DEBUG
	XLog4(pApp, "DVDWriteCells: 2");
#endif


	if ((streamout = open(targetname, O_WRONLY | O_CREAT | O_APPEND, 0666)) == -1) {
		XLog0(pApp, _("Error creating %s"), targetname);
		perror(PACKAGE);
		free(targetname);
		return(1);
	}

#ifdef DEBUG
	XLog4(pApp, "DVDWriteCells: 3");
#endif


	if ((dvd_file = DVDOpenFile(dvd, title_set, DVD_READ_TITLE_VOBS))== 0) {
		XLog0(pApp, _("Failed opening TITLE VOB"));
		free(buffer);
		close(streamout);
		free(targetname);
		return(1);
	}

	size = 0;

	for (i=0; i<length; i++) {
		left = cell_end_sector[i] - cell_start_sector[i];
		soffset = cell_start_sector[i];

		while(left > 0) {
			to_read = left;
			if (to_read + size > MAX_VOB_SIZE) {
				to_read = MAX_VOB_SIZE - size;
			}
			if (to_read > BUFFER_SIZE) {
				to_read = BUFFER_SIZE;
			}

			if ((have_read = DVDReadBlocks(dvd_file,soffset, to_read, buffer)) < 0) {
				XLog0(pApp, _("Error reading MENU VOB: %d != %d"), have_read, to_read);
				free(buffer);
				DVDCloseFile(dvd_file);
				close(streamout);
				free(targetname);
				return(1);
			}
			if (have_read < to_read) {
				XLog2(pApp, _("DVDReadBlocks read %d blocks of %d blocks"), have_read, to_read);
			}
			if (write(streamout, buffer, have_read * DVD_VIDEO_LB_LEN) != have_read * DVD_VIDEO_LB_LEN) {
				XLog0(pApp, _("Error writing TITLE VOB"));
				free(buffer);
				close(streamout);
				free(targetname);
				return(1);
			}
#ifdef DEBUG
			XLog4(pApp, "Current soffset changed from %i to %i", soffset, soffset + have_read);
#endif
			soffset = soffset + have_read;
			left = left - have_read;
			size = size + have_read;
			if ((size >= MAX_VOB_SIZE) && (left > 0)) {
#ifdef DEBUG
				XLog4(pApp, "size: %i, MAX_VOB_SIZE: %i", size, MAX_VOB_SIZE);
#endif
				close(streamout);
				vob = vob + 1;
				size = 0;
				snprintf(targetname, targetname_length, "%s/%s/VIDEO_TS/VTS_%02i_%i.VOB", targetdir, title_name, title_set, vob);
				if ((streamout = open(targetname, O_WRONLY | O_CREAT | O_APPEND, 0666)) == -1) {
					XLog0(pApp, _("Error creating %s"), targetname);
					perror(PACKAGE);
					free(targetname);
					return(1);
				}
			}
		}
	}

	DVDCloseFile(dvd_file);
	free(buffer);
	close(streamout);
	free(targetname);

	return(0);
}



static void FreeSortArrays( int chapter_chapter_array[], int title_set_chapter_array[],
		int angle_angle_array[], int title_set_angle_array[],
		int subpicture_sub_array[], int title_set_sub_array[],
		int audio_audio_array[], int title_set_audio_array[],
		int size_size_array[], int title_set_size_array[],
		int channels_channel_array[], int title_set_channel_array[]) {


	free(chapter_chapter_array);
	free(title_set_chapter_array);

	free(angle_angle_array);
	free(title_set_angle_array);

	free(subpicture_sub_array);
	free(title_set_sub_array);

	free(audio_audio_array);
	free(title_set_audio_array);

	free(size_size_array);
	free(title_set_size_array);

	free(channels_channel_array);
	free(title_set_channel_array);
}


static titles_info_t * DVDGetInfo(dvd_reader_t * _dvd) {

	/* title interation */
	int counter, i, f;

	/* Our guess */
	int candidate;
	int multi = 0;
	int dual = 0;


	int titles;
	int title_sets;

	/* Arrays for chapter, angle, subpicture, audio, size, aspect, channels - file_set relationship */

	/* Size == number_of_titles */
	int * chapter_chapter_array;
	int * title_set_chapter_array;

	int * angle_angle_array;
	int * title_set_angle_array;

	/* Size == number_of_title_sets */

	int * subpicture_sub_array;
	int * title_set_sub_array;

	int * audio_audio_array;
	int * title_set_audio_array;

	int * size_size_array;
	int * title_set_size_array;

	int * channels_channel_array;
	int * title_set_channel_array;

	/* Temp helpers */
	int channels;
	int found;
	int chapters_1;
	int chapters_2;
	int found_chapter;
	int number_of_multi;


	/* DVD handlers */
	ifo_handle_t* vmg_ifo = NULL;
	dvd_file_t* vts_title_file = NULL;

	titles_info_t* titles_info = NULL;

	/* Open main info file */
	vmg_ifo = ifoOpen( _dvd, 0 );
	if(!vmg_ifo) {
		XLog0(pApp, _("Cannot open VMG info."));
		return (0);
	}

	titles = vmg_ifo->tt_srpt->nr_of_srpts;
	title_sets = vmg_ifo->vmgi_mat->vmg_nr_of_title_sets;

	if ((vmg_ifo->tt_srpt == 0) || (vmg_ifo->vts_atrt == 0)) {
		ifoClose(vmg_ifo);
		return(0);
	}


	if((titles_info = (titles_info_t *)malloc(sizeof(titles_info_t))) == NULL) {
		XLog0(pApp, _("Out of memory creating titles info structure"));
		return NULL;
	}

	titles_info->titles = (titles_t *)malloc((titles)* sizeof(titles_t));
	titles_info->number_of_titles = titles;


	chapter_chapter_array = malloc(titles * sizeof(int));
	title_set_chapter_array = malloc(titles * sizeof(int));

	/*currently not used in the guessing */
	angle_angle_array = malloc(titles * sizeof(int));
	title_set_angle_array = malloc(titles * sizeof(int));


	subpicture_sub_array = malloc(title_sets * sizeof(int));
	title_set_sub_array = malloc(title_sets * sizeof(int));

	audio_audio_array = malloc(title_sets * sizeof(int));
	title_set_audio_array = malloc(title_sets * sizeof(int));

	size_size_array = malloc(title_sets * sizeof(int));
	title_set_size_array = malloc(title_sets * sizeof(int));

	channels_channel_array = malloc(title_sets * sizeof(int));
	title_set_channel_array = malloc(title_sets * sizeof(int));

	/* Check mallocs */
	if(!titles_info->titles || !chapter_chapter_array || !title_set_chapter_array ||
			!angle_angle_array || !title_set_angle_array || !subpicture_sub_array ||
			!title_set_sub_array || !audio_audio_array || !title_set_audio_array ||
			!size_size_array || !title_set_size_array || !channels_channel_array ||
			!title_set_channel_array) {
		XLog0(pApp, _("Out of memory creating arrays for titles info"));
		free(titles_info);
		FreeSortArrays( chapter_chapter_array, title_set_chapter_array,
						angle_angle_array, title_set_angle_array,
						subpicture_sub_array, title_set_sub_array,
						audio_audio_array, title_set_audio_array,
						size_size_array, title_set_size_array,
						channels_channel_array, title_set_channel_array);
		return NULL;
	}

	/* Interate over the titles nr_of_srpts */
	for(counter = 0; counter < titles; counter++) {
		/* For titles_info */
		titles_info->titles[counter].title = counter + 1;
		titles_info->titles[counter].title_set = vmg_ifo->tt_srpt->title[counter].title_set_nr;
		titles_info->titles[counter].vts_title = vmg_ifo->tt_srpt->title[counter].vts_ttn;
		titles_info->titles[counter].chapters = vmg_ifo->tt_srpt->title[counter].nr_of_ptts;
		titles_info->titles[counter].angles = vmg_ifo->tt_srpt->title[counter].nr_of_angles;

		/* For main title*/
		chapter_chapter_array[counter] = vmg_ifo->tt_srpt->title[counter].nr_of_ptts;
		title_set_chapter_array[counter] = vmg_ifo->tt_srpt->title[counter].title_set_nr;
		angle_angle_array[counter] = vmg_ifo->tt_srpt->title[counter].nr_of_angles;
		title_set_angle_array[counter] = vmg_ifo->tt_srpt->title[counter].title_set_nr;
	}

	/* Interate over vmg_nr_of_title_sets */

	for(counter = 0; counter < title_sets; counter++) {

		/* Picture*/
		subpicture_sub_array[counter] = vmg_ifo->vts_atrt->vts[counter].nr_of_vtstt_subp_streams;
		title_set_sub_array[counter] = counter + 1;


		/* Audio */
		audio_audio_array[counter] = vmg_ifo->vts_atrt->vts[counter].nr_of_vtstt_audio_streams;
		title_set_audio_array[counter] = counter + 1;

		channels=0;
		for(i = 0; i < audio_audio_array[counter]; i++) {
			if ( channels < vmg_ifo->vts_atrt->vts[counter].vtstt_audio_attr[i].channels + 1) {
				channels = vmg_ifo->vts_atrt->vts[counter].vtstt_audio_attr[i].channels + 1;
			}

		}
		channels_channel_array[counter] = channels;
		title_set_channel_array[counter] = counter + 1;

		/* For titles_info */
		for (f=0; f < titles_info->number_of_titles ; f++ ) {
			if ( titles_info->titles[f].title_set == counter + 1 ) {
				titles_info->titles[f].aspect_ratio = vmg_ifo->vts_atrt->vts[counter].vtstt_vobs_video_attr.display_aspect_ratio;
				titles_info->titles[f].sub_pictures = vmg_ifo->vts_atrt->vts[counter].nr_of_vtstt_subp_streams;
				titles_info->titles[f].audio_tracks = vmg_ifo->vts_atrt->vts[counter].nr_of_vtstt_audio_streams;
				titles_info->titles[f].audio_channels = channels;
			}
		}

	}




	for (counter=0; counter < title_sets; counter++ ) {

		vts_title_file = DVDOpenFile(_dvd, counter + 1, DVD_READ_TITLE_VOBS);

		if(vts_title_file != 0) {
			size_size_array[counter] = DVDFileSize(vts_title_file);
			DVDCloseFile(vts_title_file);
		} else {
			size_size_array[counter] = 0;
		}

		title_set_size_array[counter] = counter + 1;


	}


	/* Sort all arrays max to min */

	bsort_max_to_min(chapter_chapter_array, title_set_chapter_array, titles);
	bsort_max_to_min(angle_angle_array, title_set_angle_array, titles);
	bsort_max_to_min(subpicture_sub_array, title_set_sub_array, title_sets);
	bsort_max_to_min(audio_audio_array, title_set_audio_array, title_sets);
	bsort_max_to_min(size_size_array, title_set_size_array, title_sets);
	bsort_max_to_min(channels_channel_array, title_set_channel_array, title_sets);


	/* Check if the second biggest one actually can be a feature title */
	/* Here we will take do biggest/second and if that is bigger than one it's not a feauture title */
	/* Now this is simply not enough since we have to check that the diff between the two of them is small enough
	 to consider the second one a feature title we are doing two checks (biggest + biggest - second) /second == 1
	 and biggest%second * 3 < biggest */

	if ( title_sets > 1 && CheckSizeArray(size_size_array, 0, 1) == 1 ) {
		/* We have a dual DVD with two feature films - now let's see if they have the same amount of chapters*/

		chapters_1 = 0;
		for (i=0 ; i < titles ; i++ ) {
			if (titles_info->titles[i].title_set == title_set_size_array[0] ) {
				if ( chapters_1 < titles_info->titles[i].chapters){
					chapters_1 = titles_info->titles[i].chapters;
				}
			}
		}

		chapters_2 = 0;
		for (i=0 ; i < titles ; i++ ) {
			if (titles_info->titles[i].title_set == title_set_size_array[1] ) {
				if ( chapters_2 < titles_info->titles[i].chapters){
					chapters_2 = titles_info->titles[i].chapters;
				}
			}
		}

		if(vmg_ifo->vts_atrt->vts[title_set_size_array[0] - 1].vtstt_vobs_video_attr.display_aspect_ratio ==
			vmg_ifo->vts_atrt->vts[title_set_size_array[1] - 1].vtstt_vobs_video_attr.display_aspect_ratio) {
			/* In this case it's most likely so that we have a dual film but with different context
			They are with in the same size range and have the same aspect ratio
			I would guess that such a case is e.g. a DVD containing several episodes of a TV serie*/
			candidate = title_set_size_array[0];
			multi = 1;
		} else if ( chapters_1 == chapters_2 && vmg_ifo->vts_atrt->vts[title_set_size_array[0] - 1].vtstt_vobs_video_attr.display_aspect_ratio !=
			vmg_ifo->vts_atrt->vts[title_set_size_array[1] - 1].vtstt_vobs_video_attr.display_aspect_ratio){
			/* In this case we have (guess only) the same context - they have the same number of chapters but different aspect ratio and are in the same size range*/
			if ( vmg_ifo->vts_atrt->vts[title_set_size_array[0] - 1].vtstt_vobs_video_attr.display_aspect_ratio == aspect) {
				candidate = title_set_size_array[0];
			} else if ( vmg_ifo->vts_atrt->vts[title_set_size_array[1] - 1].vtstt_vobs_video_attr.display_aspect_ratio == aspect) {
				candidate = title_set_size_array[1];
			} else {
				/* Okay we didn't have the prefered aspect ratio - just make the biggest one a candidate */
				/* please send report if this happens*/
				XLog0(pApp, _("You have encountered a very special DVD; please send a bug report along with all IFO files from this title"));
				candidate = title_set_size_array[0];
			}
			dual = 1;
		} else {
			/* Can this case ever happen? */
			candidate = title_set_size_array[0];
		}
	} else {
		candidate = title_set_size_array[0];
	}


	/* Lets start checking audio,sub pictures and channels my guess is namly that a special suburb will put titles with a lot of
	 chapters just to make our backup hard */


	found = CheckAudioSubChannels(audio_audio_array, title_set_audio_array,
			subpicture_sub_array, title_set_sub_array,
			channels_channel_array, title_set_channel_array,
			0 , candidate, title_sets);


	/* Now let's see if we can find our candidate among the top most chapters */
	found_chapter=6;
	for (i=0 ; (i < titles) && (i < 4) ; i++ ) {
		if ( candidate == title_set_chapter_array[i] ) {
			found_chapter=i+1;
			break;
		}
	}

	/* Close the VMG ifo file we got all the info we need */
	ifoClose(vmg_ifo);


	if (((found == 3) && (found_chapter == 1) && (dual == 0) && (multi == 0)) || ((found == 3) && (found_chapter < 3 ) && (dual == 1))) {

		FreeSortArrays( chapter_chapter_array, title_set_chapter_array,
				angle_angle_array, title_set_angle_array,
				subpicture_sub_array, title_set_sub_array,
				audio_audio_array, title_set_audio_array,
				size_size_array, title_set_size_array,
				channels_channel_array, title_set_channel_array);
		titles_info->main_title_set = candidate;
		return(titles_info);

	}

	if (multi == 1) {
		for (i=0 ; i < title_sets ; ++i) {
			if (CheckSizeArray(size_size_array, 0, i + 1) == 0) {
				break;
			}
		}
		number_of_multi = i;
		for (i = 0; i < number_of_multi; i++ ) {
			if (title_set_chapter_array[0] == i + 1) {
				candidate = title_set_chapter_array[0];
			}
		}

		found = CheckAudioSubChannels(audio_audio_array, title_set_audio_array,
				subpicture_sub_array, title_set_sub_array,
				channels_channel_array, title_set_channel_array,
				0 , candidate, title_sets);

		if (found == 3) {
			FreeSortArrays( chapter_chapter_array, title_set_chapter_array,
					angle_angle_array, title_set_angle_array,
					subpicture_sub_array, title_set_sub_array,
					audio_audio_array, title_set_audio_array,
					size_size_array, title_set_size_array,
					channels_channel_array, title_set_channel_array);
			titles_info->main_title_set = candidate;
			return(titles_info);
		}
	}

	/* We have now come to that state that we more or less have given up :( giving you a good guess of the main feature film*/
	/*No matter what we will more or less only return the biggest VOB*/
	/* Lets see if we can find our biggest one - then we return that one */
	candidate = title_set_size_array[0];

	found = CheckAudioSubChannels(audio_audio_array, title_set_audio_array,
			subpicture_sub_array, title_set_sub_array,
			channels_channel_array, title_set_channel_array,
			0 , candidate, title_sets);

	/* Now let's see if we can find our candidate among the top most chapters */

	found_chapter=5;
	for (i=0 ; (i < titles) && (i < 4) ; i++ ) {
		if ( candidate == title_set_chapter_array[i] ) {
			found_chapter=i+1;
			break;
		}

	}

	/* Here we take chapters in to consideration*/
	if (found == 3) {
		FreeSortArrays( chapter_chapter_array, title_set_chapter_array,
				angle_angle_array, title_set_angle_array,
				subpicture_sub_array, title_set_sub_array,
				audio_audio_array, title_set_audio_array,
				size_size_array, title_set_size_array,
				channels_channel_array, title_set_channel_array);
		titles_info->main_title_set = candidate;
		return(titles_info);
	}

	/* Here we do but we lower the treshold for audio, sub and channels */

	if ((found > 1 ) && (found_chapter <= 4)) {
		FreeSortArrays( chapter_chapter_array, title_set_chapter_array,
				angle_angle_array, title_set_angle_array,
				subpicture_sub_array, title_set_sub_array,
				audio_audio_array, title_set_audio_array,
				size_size_array, title_set_size_array,
				channels_channel_array, title_set_channel_array);
		titles_info->main_title_set = candidate;
		return(titles_info);

		/* return it */
	} else {
		/* Here we give up and just return the biggest one :(*/
		/* Just return the biggest badest one*/
		FreeSortArrays( chapter_chapter_array, title_set_chapter_array,
				angle_angle_array, title_set_angle_array,
				subpicture_sub_array, title_set_sub_array,
				audio_audio_array, title_set_audio_array,
				size_size_array, title_set_size_array,
				channels_channel_array, title_set_channel_array);
		titles_info->main_title_set = candidate;
		return(titles_info);
	}


	/* Some radom thoughts about DVD guessing */
	/* We will now gather as much data about the DVD-Video as we can and
	then make a educated guess which one is the main feature film of it*/


	/* Make a tripple array with chapters, angles and title sets
	 - sort out dual title sets with a low number of chapters. Tradtionaly
	 the title set with most chapters is the main film. Number of angles is
	 keept as a reference point of low value*/

	/* Make a dual array with number of audio streams, sub picture streams
	 and title sets. Tradtionaly the main film has many audio streams
	 since it's supposed be synchronised e.g. a English film synchronised/dubbed
	 in German. We are also keeping track of sub titles since it's also indication
	 of the main film*/

	/* Which title set is the biggest one - dual array with title sets and size
	 The biggest one is usally the main film*/

	/* Which title set is belonging to title 1 and how many chapters has it. Once
	 again tradtionaly title one is belonging to the main film*/

	/* Yes a lot of rant - but it helps me think - some sketch on paper or in the mind
	 I sketch in the comments - beside it will help you understand the code*/

	/* Okay let's see if the biggest one has most chapters, it also has more subtitles
	 and audio tracks than the second one and it's title one.
	 Done it must be the main film

	 Hmm the biggest one doesn't have the most chapters?

	 See if the second one has the same amount of chapters and is the biggest one
	 If so we probably have a 4:3 and 16:9 versions of film on the same disk

	 Now we fetch the 16:9 by default unless the forced to do 4:3
	 First check which one is which.
	 If the 16:9 is the biggest one and has the same or more subtitle, audio streams
	 then we are happy unless we are in force 4:3 mode :(
	 The same goes in reverse if we are in force 4:3 mode


	 Hmm, in force 4:3 mode - now we check how much smaller than the biggest one it is
	 (or the reverse if we are in 16:9 mode)

	 Generally a reverse division should render in 1 and with a small modulo - like wise
	 a normal modulo should give us a high modulo

	 If we get more than one it's of cource a fake however if we get just one we still need to check
	 if we subtract the smaller one from the bigger one we should end up with a small number - hence we
	 need to multiply it more than 4 times to get it bigger than the biggest one. Now we know that the
	 two biggest once are really big and possibly carry the same film in differnet formats.

	 We will now return the prefered one either 16:9 or 4:3 but we will first check that the one
	 we return at lest has two or more audio tracks. We don't want it if the other one has a lot
	 more sound (we may end up with a film that only has 2ch Dolby Digital so we want to check for
	 6ch DTS or Dolby Digital. If the prefered one doesn't have those features but the other once has
	 we will return the other one.
	 */

}


static int DVDCopyBlocks(dvd_file_t* dvd_file, int destination, int offset, int size, char* filename, read_error_strategy_t errorstrat, dvd_reader_t *dvd, int title_set) {
	int i;

	/* all sizes are in DVD logical blocks */
	int remaining = size;
	int total = size; // total size in blocks
	float totalMiB = (float)(total) / 512.0f; // total size in [MiB]
	int to_read = BUFFER_SIZE;
	int act_read; /* number of buffers actually read */

	/* Write buffer */
	unsigned char buffer[BUFFER_SIZE * DVD_VIDEO_LB_LEN];
	unsigned char buffer_zero[BUFFER_SIZE * DVD_VIDEO_LB_LEN];

#ifdef FIND_UNUSED
	GSList *range_list = NULL;

	if(errorstrat == STRATEGY_SKIP_UNUSED)
		create_titleset_range_list(dvd, title_set, &range_list);
#endif


	for(i = 0; i < BUFFER_SIZE * DVD_VIDEO_LB_LEN; i++) {
		buffer_zero[i] = '\0';
	}

	while( remaining > 0 ) {

		to_read = BUFFER_SIZE;

		if (to_read > remaining) {
			to_read = remaining;
		}

#ifdef FIND_UNUSED
		/* skip or blank out unused blocks */
		if(errorstrat == STRATEGY_SKIP_UNUSED)
		{
			int next_sectors = find_next_sectors(range_list, offset);
//			fprintf(stderr, "offset %d next_sectors %d\n", offset, next_sectors);
			if(next_sectors > 0)
			{
				if(next_sectors < to_read)
					to_read = next_sectors;
			}
			else if(next_sectors < 0)
			{
				int missing = -next_sectors;
				static unsigned char StuffingPackHead[20] = {0x00,0x00,0x01,0xBA,0x44,0x00,0x04,0x00,0x04,0x01,0x01,0x89,0xC3,0xF8,0x00,0x00,0x01,0xBE,0x07,0xEC};
				int i;
				if(missing > to_read)
					missing = to_read;
				memset(buffer, 0, missing * 2048);
				for(i = 0; i < missing ; i++)
				{
					memcpy(buffer + i*2048, StuffingPackHead, 20);
				}
#ifdef PAD_SKIPPED_BLOCK
				if(write(destination,buffer,missing *  2048) != missing * 2048)
				fprintf(stderr, "writing %d padded blocks\n", missing);
#else
				fprintf(stderr, "seeking over %d unreferenced blocks\n", missing);
				if(lseek(destination, missing *2048, SEEK_CUR) < 0)
#endif
				{
					fprintf(stderr, "Error writing TITLE VOB\n");
					DVDCloseFile(dvd_file);
					close(destination);
					return(1);
				}

				remaining -= missing;
				offset += missing;
				continue;
			}
		}
#endif


		/* Reading blocks */
		act_read = DVDReadBlocks(dvd_file, offset, to_read, buffer);

		if(act_read != to_read) {
			if(act_read >= 0) {
				XLog0(pApp, _("Error reading %s at block %d"), filename, offset+act_read);
			} else {
				XLog0(pApp, _("Error reading %s at block %d, read error returned"), filename, offset);
			}
		}

		if(act_read > 0) {
			/* Writing blocks */
			if(write(destination, buffer, act_read * DVD_VIDEO_LB_LEN) != act_read * DVD_VIDEO_LB_LEN) {
				XLog0(pApp, _("Error writing %s."), filename);
				return(1);
			}

			offset += act_read;
			remaining -= act_read;
		}

		if(act_read != to_read) {
			int numBlanks = 0;

			if (act_read < 0) {
				act_read = 0;
			}

			switch (errorstrat) {
			case STRATEGY_ABORT:
				XLog0(pApp, _("aborting"));
				return 1;

			case STRATEGY_SKIP_BLOCK:
				numBlanks = 1;
				XLog1(pApp, _("padding single block"));
				break;

			case STRATEGY_SKIP_MULTIBLOCK:
				numBlanks = to_read - act_read;
				XLog1(pApp, _("padding %d blocks"), numBlanks);
				break;
#ifdef FIND_UNUSED
			case STRATEGY_SKIP_UNUSED:
				fprintf(stderr, "bad block, even when skipping unused. Falling back to skip multiblock.\n");
#endif
				break;
			}

			if (write(destination, buffer_zero, numBlanks * DVD_VIDEO_LB_LEN) != numBlanks * DVD_VIDEO_LB_LEN) {
				XLog0(pApp, _("Error writing %s (padding)"), filename);
				return 1;
			}

			/* pretend we read what we padded */
			offset += numBlanks;
			remaining -= numBlanks;
		}

		if(progress) {
			int done = total - remaining; // blocks done
			if(remaining < BUFFER_SIZE || (done % BUFFER_SIZE) == 0) { // don't print too often
				float doneMiB = (float)(done) / 512.0f; // [MiB] done
				XLog5(pApp, _("Copying %s: %.0f%% done (%.0f/%.0f MiB)"),
						progressText, doneMiB / totalMiB * 100.0f, doneMiB, totalMiB);
			}
		}

	}

	if (remaining == 0) {
		XLog2(pApp, _("Success writing %s"), filename);
	}

	return 0;
}



static int DVDCopyTitleVobX(dvd_reader_t * dvd, title_set_info_t * title_set_info, int title_set, int vob, char * targetdir,char * title_name, read_error_strategy_t errorstrat) {

	/* Loop variable */
	int i;

	/* Temp filename,dirname */
	// filename is either "VIDEO_TS.VOB" or "VTS_XX_X.VOB" and terminating "\0"
	char filename[13] = "VIDEO_TS.VOB";
	char *targetname;
	size_t targetname_length;
	struct stat fileinfo;

	/* File Handler */
	int streamout;

	int size;

	int offset = 0;
	int tsize;

	/* DVD handler */
	dvd_file_t* dvd_file=NULL;

	/* Return value */
	int result;

	/* create filename VIDEO_TS.VOB or VTS_XX_X.VOB */
	if(title_set > 0) {
		sprintf(filename, "VTS_%02i_%i.VOB", title_set, vob);
	}


	if (title_set_info->number_of_title_sets + 1 < title_set) {
		XLog0(pApp, _("Failed num title test"));
		return(1);
	}

	if (title_set_info->title_set[title_set].number_of_vob_files < vob ) {
		XLog0(pApp, _("Failed vob test"));
		return(1);
	}

	if (title_set_info->title_set[title_set].size_vob[0] == 0 ) {
		XLog1(pApp, _("Failed vob 1 size test"));
		return(0);
	} else if (title_set_info->title_set[title_set].size_vob[vob - 1] == 0 ) {
		XLog1(pApp, _("Failed vob %d test"), vob);
		return(0);
	} else {
		size = title_set_info->title_set[title_set].size_vob[vob - 1]/DVD_VIDEO_LB_LEN;
		if (title_set_info->title_set[title_set].size_vob[vob - 1]%DVD_VIDEO_LB_LEN != 0) {
			XLog0(pApp, _("The Title VOB number %d of title set %d does not have a valid DVD size"), vob, title_set);
			return(1);
		}
	}
#ifdef DEBUG
	XLog4(pApp, "After we check the vob it self %d", vob);
#endif

	/* Create VTS_XX_X.VOB */
	if (title_set == 0) {
		XLog1(pApp, _("Do not try to copy a Title VOB from the VMG domain; there are none."));
		return(1);
	} else {
		// Reserve space for "<targetdir>/<title_name>/VIDEO_TS/<filename>" and terminating "\0"
		targetname_length = strlen(targetdir) + strlen(title_name) + strlen(filename) + 12;
		targetname = malloc(targetname_length);
		if (targetname == NULL) {
			XLog0(pApp, _("Failed to allocate %zu bytes for a filename."), targetname_length);
			return 1;
		}
		snprintf(targetname, targetname_length, "%s/%s/VIDEO_TS/%s", targetdir, title_name, filename);
	}



	/* Now figure out the offset we will start at also check that the previus files are of valid DVD size */
	for ( i = 0; i < vob - 1; i++ ) {
		tsize = title_set_info->title_set[title_set].size_vob[i];
		if (tsize%DVD_VIDEO_LB_LEN != 0) {
			XLog0(pApp, _("The Title VOB number %d of title set %d does not have a valid DVD size"), i + 1, title_set);
			free(targetname);
			return(1);
		} else {
			offset = offset + tsize/DVD_VIDEO_LB_LEN;
		}
	}
#ifdef DEBUG
	XLog4(pApp, "The offset for vob %d is %d", vob, offset);
#endif


	if (stat(targetname, &fileinfo) == 0) {
		/* TRANSLATORS: The sentence starts with "The title file %s exists[...]" */
		XLog1(pApp, _("The %s %s exists; will try to overwrite it."), _("title file"), targetname);
		if (! S_ISREG(fileinfo.st_mode)) {
			/* TRANSLATORS: The sentence starts with "The title file %s is not valid[...]" */
			XLog1(pApp, _("The %s %s is not valid, it may be a directory."), _("title file"), targetname);
			free(targetname);
			return(1);
		} else {
			if ((streamout = open(targetname, O_WRONLY | O_TRUNC, 0666)) == -1) {
				XLog0(pApp, _("Error opening %s"), targetname);
				perror(PACKAGE);
				free(targetname);
				return(1);
			}
		}
	} else {
		if ((streamout = open(targetname, O_WRONLY | O_CREAT, 0666)) == -1) {
			XLog0(pApp, _("Error creating %s"), targetname);
			perror(PACKAGE);
			free(targetname);
			return(1);
		}
	}

	if ((dvd_file = DVDOpenFile(dvd, title_set, DVD_READ_TITLE_VOBS))== 0) {
		XLog0(pApp, _("Failed opening TITLE VOB"));
		close(streamout);
		free(targetname);
		return(1);
	}

	result = DVDCopyBlocks(dvd_file, streamout, offset, size, filename, errorstrat, dvd, title_set);

	DVDCloseFile(dvd_file);
	close(streamout);
	free(targetname);
	return result;
}


static int DVDCopyMenu(dvd_reader_t * dvd, title_set_info_t * title_set_info, int title_set, char * targetdir,char * title_name, read_error_strategy_t errorstrat) {

	/* Temp filename,dirname */
	// filename is either "VIDEO_TS.VOB" or "VTS_XX_0.VOB" and terminating "\0"
	char filename[13] = "VIDEO_TS.VOB";
	char *targetname;
	size_t targetname_length;
	struct stat fileinfo;

	/* File Handler */
	int streamout;

	int size;

	/* return value */
	int result;

	/* DVD handler */
	dvd_file_t* dvd_file = NULL;

	/* create filename VIDEO_TS.VOB or VTS_XX_0.VOB */
	if(title_set > 0) {
		sprintf(filename, "VTS_%02i_0.VOB", title_set);
	}

	if (title_set_info->number_of_title_sets + 1 < title_set) {
		return(1);
	}

	if (title_set_info->title_set[title_set].size_menu == 0 ) {
		return(0);
	} else {
		size = title_set_info->title_set[title_set].size_menu/DVD_VIDEO_LB_LEN;
		if (title_set_info->title_set[title_set].size_menu%DVD_VIDEO_LB_LEN != 0) {
			XLog1(pApp, _("Warning: The Menu VOB of title set %d (%s) does not have a valid DVD size."), title_set, filename);
		}
	}

	if ((dvd_file = DVDOpenFile(dvd, title_set, DVD_READ_MENU_VOBS))== 0) {
		XLog0(pApp, _("Failed opening %s"), filename);
		return(1);
	}

	// Reserve space for "<targetdir>/<title_name>/VIDEO_TS/<filename>" and terminating "\0"
	targetname_length = strlen(targetdir) + strlen(title_name) + strlen(filename) + 12;
	targetname = malloc(targetname_length);
	if (targetname == NULL) {
		XLog0(pApp, _("Failed to allocate %zu bytes for a filename."), targetname_length);
		return 1;
	}
	/* Create VIDEO_TS.VOB or VTS_XX_0.VOB */
	snprintf(targetname, targetname_length, "%s/%s/VIDEO_TS/%s", targetdir, title_name, filename);

	if (stat(targetname, &fileinfo) == 0) {
		/* TRANSLATORS: The sentence starts with "The menu file %s exists[...]" */
		XLog1(pApp, _("The %s %s exists; will try to overwrite it."), _("menu file"), targetname);
		if (! S_ISREG(fileinfo.st_mode)) {
			/* TRANSLATORS: The sentence starts with "The menu file %s is not valid[...]" */
			XLog1(pApp, _("The %s %s is not valid, it may be a directory."), _("menu file"), targetname);
			DVDCloseFile(dvd_file);
			free(targetname);
			return(1);
		} else {
			if ((streamout = open(targetname, O_WRONLY | O_TRUNC, 0666)) == -1) {
				XLog0(pApp, _("Error opening %s"), targetname);
				perror(PACKAGE);
				DVDCloseFile(dvd_file);
				free(targetname);
				return(1);
			}
		}
	} else {
		if ((streamout = open(targetname, O_WRONLY | O_CREAT, 0666)) == -1) {
			XLog0(pApp, _("Error creating %s"), targetname);
			perror(PACKAGE);
			DVDCloseFile(dvd_file);
			free(targetname);
			return(1);
		}
	}

	if(progress) {
		strncpy(progressText, _("menu"), MAXNAME);
	}

	result = DVDCopyBlocks(dvd_file, streamout, 0, size, filename, errorstrat, dvd, title_set);

	DVDCloseFile(dvd_file);
	close(streamout);
	free(targetname);
	return result;

}


static int DVDCopyIfoBup(dvd_reader_t* dvd, title_set_info_t* title_set_info, int title_set, char* targetdir, char* title_name) {
	/* Temp filename, dirname */
	char *targetname_ifo;
	char *targetname_bup;
	size_t string_length;
	struct stat fileinfo;

	/* Write buffer */
	unsigned char* buffer = NULL;

	/* File Handler */
	int streamout_ifo = -1, streamout_bup = -1;

	int size;

	/* DVD handler */
	ifo_handle_t* ifo_file = NULL;
	struct ifo_handle_private_s *ifop = NULL;


	if (title_set_info->number_of_title_sets + 1 < title_set) {
		return(1);
	}

	if (title_set_info->title_set[title_set].size_ifo == 0 ) {
		return(0);
	} else {
		if (title_set_info->title_set[title_set].size_ifo%DVD_VIDEO_LB_LEN != 0) {
			XLog0(pApp, _("The IFO of title set %d does not have a valid DVD size"), title_set);
			return(1);
		}
	}

	// Reserve space for "<targetdir>/<title_name>/VIDEO_TS/VIDEO_TS.IFO" or
	// "<targetdir>/<title_name>/VIDEO_TS/VTS_XX_0.IFO" and terminating "\0"
	string_length = strlen(targetdir) + strlen(title_name) + 24;
	targetname_ifo = malloc(string_length);
	targetname_bup = malloc(string_length);
	if (targetname_ifo == NULL || targetname_bup == NULL) {
		XLog0(pApp, _("Failed to allocate %zu bytes for a filename."), string_length);
		free(targetname_ifo);
		free(targetname_bup);
		return 1;
	}

	/* Create VIDEO_TS.IFO or VTS_XX_0.IFO */

	if (title_set == 0) {
		snprintf(targetname_ifo, string_length, "%s/%s/VIDEO_TS/VIDEO_TS.IFO", targetdir, title_name);
		snprintf(targetname_bup, string_length, "%s/%s/VIDEO_TS/VIDEO_TS.BUP", targetdir, title_name);
	} else {
		snprintf(targetname_ifo, string_length, "%s/%s/VIDEO_TS/VTS_%02i_0.IFO", targetdir, title_name, title_set);
		snprintf(targetname_bup, string_length, "%s/%s/VIDEO_TS/VTS_%02i_0.BUP", targetdir, title_name, title_set);
	}

	if (stat(targetname_ifo, &fileinfo) == 0) {
		/* TRANSLATORS: The sentence starts with "The IFO file %s exists[...]" */
		XLog1(pApp, _("The %s %s exists; will try to overwrite it."), _("IFO file"), targetname_ifo);
		if (! S_ISREG(fileinfo.st_mode)) {
			/* TRANSLATORS: The sentence starts with "The IFO file %s is not valid[...]" */
			XLog1(pApp, _("The %s %s is not valid, it may be a directory."), _("IFO file"), targetname_ifo);
			free(targetname_ifo);
			free(targetname_bup);
			return(1);
		}
	}

	if (stat(targetname_bup, &fileinfo) == 0) {
		/* TRANSLATORS: The sentence starts with "The BUP file %s exists[...]" */
		XLog1(pApp, _("The %s %s exists; will try to overwrite it."), _("BUP file"), targetname_bup);
		if (! S_ISREG(fileinfo.st_mode)) {
			/* TRANSLATORS: The sentence starts with "The BUP file %s is not valid[...]" */
			XLog1(pApp, _("The %s %s is not valid, it may be a directory."), _("BUP file"), targetname_bup);
			free(targetname_ifo);
			free(targetname_bup);
			return(1);
		}
	}

	if ((streamout_ifo = open(targetname_ifo, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1) {
		XLog1(pApp, _("Error creating %s"), targetname_ifo);
		perror(PACKAGE);
		ifoClose(ifo_file);
		free(buffer);
		free(targetname_ifo);
		free(targetname_bup);
		close(streamout_ifo);
		close(streamout_bup);
		return 1;
	}

	if ((streamout_bup = open(targetname_bup, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1) {
		XLog1(pApp, _("Error creating %s"), targetname_bup);
		perror(PACKAGE);
		ifoClose(ifo_file);
		free(buffer);
		free(targetname_ifo);
		free(targetname_bup);
		close(streamout_ifo);
		close(streamout_bup);
		return 1;
	}

	/* Copy VIDEO_TS.IFO, since it's a small file try to copy it in one shot */

	if ((ifo_file = ifoOpen(dvd, title_set))== 0) {
		fprintf(stderr, _("Failed opening IFO for title set %d\n"), title_set);
		ifoClose(ifo_file);
		free(buffer);
		free(targetname_ifo);
		free(targetname_bup);
		close(streamout_ifo);
		close(streamout_bup);
		return 1;
	}

	ifop = PRIV(ifo_file);
	size = DVDFileSize(ifop->file) * DVD_VIDEO_LB_LEN;

	if ((buffer = (unsigned char *)malloc(size * sizeof(unsigned char))) == NULL) {
		perror(PACKAGE);
		ifoClose(ifo_file);
		free(buffer);
		free(targetname_ifo);
		free(targetname_bup);
		close(streamout_ifo);
		close(streamout_bup);
		return 1;
	}

	DVDFileSeek(ifop->file, 0);

	if (DVDReadBytes(ifop->file,buffer,size) != size) {
		XLog0(pApp, _("Error reading IFO for title set %d"), title_set);
		ifoClose(ifo_file);
		free(buffer);
		free(targetname_ifo);
		free(targetname_bup);
		close(streamout_ifo);
		close(streamout_bup);
		return 1;
	}


	if (write(streamout_ifo,buffer,size) != size) {
		XLog0(pApp, _("Error writing %s"),targetname_ifo);
		ifoClose(ifo_file);
		free(buffer);
		free(targetname_ifo);
		free(targetname_bup);
		close(streamout_ifo);
		close(streamout_bup);
		return 1;
	}

	if (write(streamout_bup,buffer,size) != size) {
		XLog0(pApp, _("Error writing %s"),targetname_bup);
		ifoClose(ifo_file);
		free(buffer);
		free(targetname_ifo);
		free(targetname_bup);
		close(streamout_ifo);
		close(streamout_bup);
		return 1;
	}

	free(targetname_ifo);
	free(targetname_bup);
	return 0;
}


static int DVDMirrorTitleX(dvd_reader_t* dvd, title_set_info_t* title_set_info,
		int title_set, char* targetdir, char* title_name,
		read_error_strategy_t errorstrat) {

	/* Loop through the vobs */
	int i;
	int n;

	if ( DVDCopyIfoBup(dvd, title_set_info, title_set, targetdir, title_name) != 0 ) {
		return(1);
	}

	if ( DVDCopyMenu(dvd, title_set_info, title_set, targetdir, title_name, errorstrat) != 0 ) {
		return(1);
	}

	n = title_set_info->title_set[title_set].number_of_vob_files;
	for (i = 0; i < n; i++) {
#ifdef DEBUG
		XLog4(pApp, "In the VOB copy loop for %d", i);
#endif
		if(progress) {
			snprintf(progressText, MAXNAME, _("Title, part %i/%i"), i+1, n);
		}

		if ( DVDCopyTitleVobX(dvd, title_set_info, title_set, i + 1, targetdir, title_name, errorstrat) != 0 ) {
		return(1);
		}
	}


	return(0);
}

int DVDGetTitleName(const char *device, char *title)
{
	/* Variables for filehandel and title string interaction */

	char tempBuf[DVD_SEC_SIZ];
	int filehandle, i;
	int length = 32;
	int word_length = 0;

	/* Open DVD device */

	if ( !(filehandle = open(device, O_RDONLY)) ) {
		XLog0(pApp, _("Cannot open specified device %s - check your DVD device"), device);
		return(1);
	}

	/* Seek to title of first track, which is at (track_no * 32768) + 40 */

	if(lseek(filehandle, 32768, SEEK_SET) != 32768) {
		close(filehandle);
		XLog0(pApp, _("Cannot seek DVD device %s - check your DVD device"), device);
		return(1);
	}

	/* Read the DVD-Video title */
	if(DVD_SEC_SIZ != read(filehandle, tempBuf, DVD_SEC_SIZ)) {
		close(filehandle);
		XLog0(pApp, _("Cannot read title from DVD device %s"), device);
		return(1);
	}
	snprintf(title, length + 1, "%s", tempBuf + 40);

	/* Remove trailing white space */
	while(title[length-1] == ' ') {
		title[length-1] = '\0';
		length--;
	}

	/* convert title to lower case and replace underscores with spaces */
	for(i = 0; i < length; i++) {
		word_length++;
		if(word_length == 1) {
			title[i] = toupper(title[i]);
		} else {
			title[i] = tolower(title[i]);
		}
		if(title[i] == '_') {
			title[i] = ' ';
		}
		if(title[i] == ' ') {
			word_length = 0;
		}
	}

	return(0);
}



static void bsort_min_to_max(int sector[], int title[], int size){

	int temp_title, temp_sector, i, j;

	for ( i=0; i < size ; i++ ) {
		for ( j=0; j < size ; j++ ) {
			if (sector[i] < sector[j]) {
				temp_sector = sector[i];
				temp_title = title[i];
				sector[i] = sector[j];
				title[i] = title[j];
				sector[j] = temp_sector;
				title[j] = temp_title;
			}
		}
	}
}

static void bsort_max_to_min(int sector[], int title[], int size){

	int temp_title, temp_sector, i, j;

	for ( i=0; i < size ; i++ ) {
		for ( j=0; j < size ; j++ ) {
			if (sector[i] > sector[j]) {
				temp_sector = sector[i];
				temp_title = title[i];
				sector[i] = sector[j];
				title[i] = title[j];
				sector[j] = temp_sector;
				title[j] = temp_title;
			}
		}
	}
}


static void align_end_sector(int cell_start_sector[],int cell_end_sector[], int size) {

	int i;

	for (i = 0; i < size - 1 ; i++) {
		if ( cell_end_sector[i] >= cell_start_sector[i + 1] ) {
			cell_end_sector[i] = cell_start_sector[i + 1] - 1;
		}
	}
}


static void DVDFreeTitleSetInfo(title_set_info_t * title_set_info) {
	free(title_set_info->title_set);
	free(title_set_info);
}


static void DVDFreeTitlesInfo(titles_info_t * titles_info) {
	free(titles_info->titles);
	free(titles_info);
}


static title_set_info_t* DVDGetFileSet(dvd_reader_t* dvd) {

	/* title interation */
	int title_sets, counter, i;

	/* DVD Video files */
	dvd_stat_t statbuf;

	/* DVD IFO handler */
	ifo_handle_t* vmg_ifo = NULL;

	/* The Title Set Info struct */
	title_set_info_t* title_set_info;

	/* Open main info file */
	vmg_ifo = ifoOpen(dvd, 0);
	if(vmg_ifo == NULL) {
		XLog0(pApp, _("Cannot open Video Manager (VMG) info."));
		return NULL;
	}

	title_sets = vmg_ifo->vmgi_mat->vmg_nr_of_title_sets;

	/* Close the VMG IFO file we got all the info we need */
	ifoClose(vmg_ifo);

	title_set_info = (title_set_info_t*)malloc(sizeof(title_set_info_t));
	if(title_set_info == NULL) {
		perror(PACKAGE);
		return NULL;
	}

	title_set_info->title_set = (title_set_t*)malloc((title_sets + 1) * sizeof(title_set_t));
	if(title_set_info->title_set == NULL) {
		perror(PACKAGE);
		free(title_set_info);
		return NULL;
	}

	title_set_info->number_of_title_sets = title_sets;


	/* Find VIDEO_TS.IFO is present - must be present since we did a ifo open 0 */

	if (DVDFileStat(dvd, 0, DVD_READ_INFO_FILE, &statbuf) != -1) {
		title_set_info->title_set[0].size_ifo = statbuf.size;
	} else {
		DVDFreeTitleSetInfo(title_set_info);
		return NULL;
	}

	/* Find VIDEO_TS.VOB if present*/

	if(DVDFileStat(dvd, 0, DVD_READ_MENU_VOBS, &statbuf) != -1) {
		title_set_info->title_set[0].size_menu = statbuf.size;
	} else {
		title_set_info->title_set[0].size_menu = 0 ;
	}

	/* Take care of the titles which we don't have in VMG */

	title_set_info->title_set[0].number_of_vob_files = 0;
	title_set_info->title_set[0].size_vob[0] = 0;

	if ( verbose > 0 ){
		XLog3(pApp, _("File sizes for Title set 0 VIDEO_TS.XXX"));
		XLog3(pApp, _("IFO = %jd, MENU_VOB = %jd"),(intmax_t)title_set_info->title_set[0].size_ifo, (intmax_t)title_set_info->title_set[0].size_menu);
	}

	for(counter = 0; counter < title_sets; counter++) {

		if(verbose > 1) {
			XLog3(pApp, _("At top of loop"));
		}


		if(DVDFileStat(dvd, counter + 1, DVD_READ_INFO_FILE, &statbuf) != -1) {
			title_set_info->title_set[counter+1].size_ifo = statbuf.size;
		} else {
			DVDFreeTitleSetInfo(title_set_info);
			return NULL;
		}

		if(verbose > 1) {
			XLog3(pApp, _("After opening files"));
		}

		/* Find VTS_XX_0.VOB if present */

		if(DVDFileStat(dvd, counter + 1, DVD_READ_MENU_VOBS, &statbuf) != -1) {
			title_set_info->title_set[counter + 1].size_menu = statbuf.size;
		} else {
			title_set_info->title_set[counter + 1].size_menu = 0 ;
		}

		if(verbose > 1) {
			XLog3(pApp, _("After Menu VOB check"));
		}

		/* Find all VTS_XX_[1 to 9].VOB files if they are present */

		i = 0;
		if(DVDFileStat(dvd, counter + 1, DVD_READ_TITLE_VOBS, &statbuf) != -1) {
			for(i = 0; i < statbuf.nr_parts; ++i) {
				title_set_info->title_set[counter + 1].size_vob[i] = statbuf.parts_size[i];
			}
		}
		title_set_info->title_set[counter + 1].number_of_vob_files = i;

		if(verbose > 1) {
			XLog3(pApp, _("After Menu Title VOB check"));
		}

		if(verbose > 0) {
			XLog3(pApp, _("File sizes for Title set %d i.e. VTS_%02d_X.XXX"), counter + 1, counter + 1);
			XLog3(pApp, _("IFO: %jd, MENU: %jd"), (intmax_t)title_set_info->title_set[counter +1].size_ifo, (intmax_t)title_set_info->title_set[counter +1].size_menu);
			for (i = 0; i < title_set_info->title_set[counter + 1].number_of_vob_files ; i++) {
				XLog3(pApp, _("VOB %d is %jd"), i + 1, (intmax_t)title_set_info->title_set[counter + 1].size_vob[i]);
			}
		}

		if(verbose > 1) {
			XLog3(pApp, _("Bottom of loop"));
		}
	}

	/* Return the info */
	return title_set_info;
}


int DVDMirror(dvd_reader_t * _dvd, char * targetdir,char * title_name, read_error_strategy_t errorstrat) {

	int i;
	title_set_info_t * title_set_info=NULL;

	title_set_info = DVDGetFileSet(_dvd);
	if (!title_set_info) {
		DVDClose(_dvd);
		return(1);
	}

	for ( i=0; i <= title_set_info->number_of_title_sets; i++) {
		if ( DVDMirrorTitleX(_dvd, title_set_info, i, targetdir, title_name, errorstrat) != 0 ) {
			XLog0(pApp, _("Mirror of Title set %d failed"), i);
			DVDFreeTitleSetInfo(title_set_info);
			return(1);
		}
	}
	return(0);
}


int DVDMirrorTitleSet(dvd_reader_t * _dvd, char * targetdir,char * title_name, int title_set, read_error_strategy_t errorstrat) {

	title_set_info_t * title_set_info=NULL;


#ifdef DEBUG
	XLog4(pApp, "In DVDMirrorTitleSet");
#endif

	title_set_info = DVDGetFileSet(_dvd);

	if (!title_set_info) {
		DVDClose(_dvd);
		return(1);
	}

	if ( title_set > title_set_info->number_of_title_sets ) {
		XLog0(pApp, _("Cannot copy title_set %d there is only %d title_sets present on this DVD"), title_set, title_set_info->number_of_title_sets);
		DVDFreeTitleSetInfo(title_set_info);
		return(1);
	}

	if ( DVDMirrorTitleX(_dvd, title_set_info, title_set, targetdir, title_name, errorstrat) != 0 ) {
		XLog0(pApp, _("Mirror of Title set %d failed"), title_set);
		DVDFreeTitleSetInfo(title_set_info);
		return(1);
	}

	DVDFreeTitleSetInfo(title_set_info);
	return(0);
}


int DVDMirrorMainFeature(dvd_reader_t * _dvd, char * targetdir,char * title_name, read_error_strategy_t errorstrat) {

	title_set_info_t * title_set_info=NULL;
	titles_info_t * titles_info=NULL;


	titles_info = DVDGetInfo(_dvd);
	if (!titles_info) {
		XLog0(pApp, _("Guesswork of main feature film failed."));
		return(1);
	}

	title_set_info = DVDGetFileSet(_dvd);
	if (!title_set_info) {
		DVDFreeTitlesInfo(titles_info);
		return(1);
	}

	if ( DVDMirrorTitleX(_dvd, title_set_info, titles_info->main_title_set, targetdir, title_name, errorstrat) != 0 ) {
		XLog0(pApp, _("Mirror of main feature file which is title set %d failed"), titles_info->main_title_set);
		DVDFreeTitleSetInfo(title_set_info);
		return(1);
	}

	DVDFreeTitlesInfo(titles_info);
	DVDFreeTitleSetInfo(title_set_info);
	return(0);
}


int DVDMirrorChapters(dvd_reader_t * _dvd, char * targetdir,char * title_name, int start_chapter,int end_chapter, int titles) {


	int result;
	int chapters = 0;
	int i, s;
	int spg, epg;
	int pgc;
	int start_cell, end_cell;
	int vts_title;

	title_set_info_t * title_set_info=NULL;
	titles_info_t * titles_info=NULL;
	ifo_handle_t * vts_ifo_info=NULL;
	int * cell_start_sector=NULL;
	int * cell_end_sector=NULL;

	titles_info = DVDGetInfo(_dvd);
	if (!titles_info) {
		XLog0(pApp, _("Failed to obtain titles information"));
		return(1);
	}

	title_set_info = DVDGetFileSet(_dvd);
	if (!title_set_info) {
		DVDFreeTitlesInfo(titles_info);
		return(1);
	}

	if(titles == 0) {
		XLog2(pApp, _("No title specified for chapter extraction, will try to figure out main feature title"));
		for (i=0; i < titles_info->number_of_titles ; i++ ) {
			if ( titles_info->titles[i].title_set == titles_info->main_title_set ) {
				if(chapters < titles_info->titles[i].chapters) {
					chapters = titles_info->titles[i].chapters;
					titles = i + 1;
				}
			}
		}
	}

	vts_ifo_info = ifoOpen(_dvd, titles_info->titles[titles - 1].title_set);
	if(!vts_ifo_info) {
		XLog0(pApp, _("Could not open title_set %d IFO file"), titles_info->titles[titles - 1].title_set);
		DVDFreeTitlesInfo(titles_info);
		DVDFreeTitleSetInfo(title_set_info);
		return(1);
	}

	vts_title = titles_info->titles[titles - 1].vts_title;

	if (end_chapter > titles_info->titles[titles - 1].chapters) {
		end_chapter = titles_info->titles[titles - 1].chapters;
		XLog1(pApp, _("Truncated the end_chapter; only %d chapters in %d title"), end_chapter,titles);
	}

	if (start_chapter > titles_info->titles[titles - 1].chapters) {
		start_chapter = titles_info->titles[titles - 1].chapters;
		XLog1(pApp, _("Truncated the end_chapter; only %d chapters in %d title"), end_chapter,titles);
	}



	/* We assume the same PGC for the whole title - this is not true and need to be fixed later on */

	pgc = vts_ifo_info->vts_ptt_srpt->title[vts_title - 1].ptt[start_chapter - 1].pgcn;


	/* Lookup PG for start chapter */

	spg = vts_ifo_info->vts_ptt_srpt->title[vts_title - 1].ptt[start_chapter - 1].pgn;

	/* Look up start cell for this pgc/pg */

	start_cell = vts_ifo_info->vts_pgcit->pgci_srp[pgc - 1].pgc->program_map[spg - 1];


	/* Lookup end cell*/


	if ( end_chapter < titles_info->titles[titles - 1].chapters ) {
		epg = vts_ifo_info->vts_ptt_srpt->title[vts_title - 1].ptt[end_chapter].pgn;
#ifdef DEBUG
		XLog4(pApp, "DVDMirrorChapter: epg %d", epg);
#endif

		end_cell = vts_ifo_info->vts_pgcit->pgci_srp[pgc - 1].pgc->program_map[epg -1] - 1;
#ifdef DEBUG
		XLog4(pApp, "DVDMirrorChapter: end cell adjusted %d", end_cell);
#endif

	} else {

		end_cell = vts_ifo_info->vts_pgcit->pgci_srp[pgc - 1].pgc->nr_of_cells;
#ifdef DEBUG
		XLog4(pApp, "DVDMirrorChapter: end cell adjusted 2 %d",end_cell);
#endif

	}

#ifdef DEBUG
	XLog4(pApp, "DVDMirrorChapter: star cell %d", start_cell);
#endif


	/* Put all the cells start and end sector in a dual array */

	cell_start_sector = (int *)malloc( (end_cell - start_cell + 1) * sizeof(int));
	if(!cell_start_sector) {
		XLog0(pApp, _("Memory allocation error 1"));
		DVDFreeTitlesInfo(titles_info);
		DVDFreeTitleSetInfo(title_set_info);
		ifoClose(vts_ifo_info);
		return(1);
	}
	cell_end_sector = (int *)malloc( (end_cell - start_cell + 1) * sizeof(int));
	if(!cell_end_sector) {
		XLog0(pApp, _("Memory allocation error"));
		DVDFreeTitlesInfo(titles_info);
		DVDFreeTitleSetInfo(title_set_info);
		ifoClose(vts_ifo_info);
		free(cell_start_sector);
		return(1);
	}
#ifdef DEBUG
	XLog4(pApp, "DVDMirrorChapter: start cell is %d", start_cell);
	XLog4(pApp, "DVDMirrorChapter: end cell is %d", end_cell);
	XLog4(pApp, "DVDMirrorChapter: pgc is %d", pgc);
#endif

	for (i=0, s=start_cell; s < end_cell +1 ; i++, s++) {

		cell_start_sector[i] = vts_ifo_info->vts_pgcit->pgci_srp[pgc - 1].pgc->cell_playback[s - 1].first_sector;
		cell_end_sector[i] = vts_ifo_info->vts_pgcit->pgci_srp[pgc - 1].pgc->cell_playback[s - 1].last_sector;
#ifdef DEBUG
		XLog4(pApp, "DVDMirrorChapter: S is %d", s);
		XLog4(pApp, "DVDMirrorChapter: start sector %d", vts_ifo_info->vts_pgcit->pgci_srp[pgc - 1].pgc->cell_playback[s - 1].first_sector);
		XLog4(pApp, "DVDMirrorChapter: end sector %d", vts_ifo_info->vts_pgcit->pgci_srp[pgc - 1].pgc->cell_playback[s - 1].last_sector);
#endif
	}

	bsort_min_to_max(cell_start_sector, cell_end_sector, end_cell - start_cell + 1);

	align_end_sector(cell_start_sector, cell_end_sector,end_cell - start_cell + 1);

#ifdef DEBUG
	for (i=0 ; i < end_cell - start_cell + 1; i++) {
		XLog4(pApp, "DVDMirrorChapter: Start sector is %d end sector is %d", cell_start_sector[i], cell_end_sector[i]);
	}
#endif

	result = DVDWriteCells(_dvd, cell_start_sector, cell_end_sector , end_cell - start_cell + 1, titles, title_set_info, titles_info, targetdir, title_name);

	DVDFreeTitlesInfo(titles_info);
	DVDFreeTitleSetInfo(title_set_info);
	ifoClose(vts_ifo_info);
	free(cell_start_sector);
	free(cell_end_sector);

	if( result != 0) {
		return(1);
	} else {
		return(0);
	}
}


int DVDMirrorTitles(dvd_reader_t * _dvd, char * targetdir,char * title_name, int titles) {

	int end_chapter;

	titles_info_t * titles_info=NULL;

#ifdef DEBUG
	XLog4(pApp, "In DVDMirrorTitles");
#endif



	titles_info = DVDGetInfo(_dvd);
	if (!titles_info) {
		XLog0(pApp, _("Failed to obtain titles information"));
		return(1);
	}


	end_chapter = titles_info->titles[titles - 1].chapters;
#ifdef DEBUG
	XLog4(pApp, "DVDMirrorTitles: end_chapter %d", end_chapter);
#endif

	if (DVDMirrorChapters( _dvd, targetdir, title_name, 1, end_chapter, titles) != 0 ) {
		DVDFreeTitlesInfo(titles_info);
		return(1);
	}

	DVDFreeTitlesInfo(titles_info);

	return(0);
}


/**
 * Formats a filesize human readable. For example 25,05 KiB instead of
 * 25648 Bytes.
 */
static void format_filesize(off_t filesize, char* result) {
	char* prefix = "";
	double size = (double)filesize;
	int prefix_count = 0;

	while(size > 1024 && prefix_count < 6) {
		size /= 1024;
		prefix_count++;
	}

	if(prefix_count == 1) {
		prefix = "Ki";
	} else if(prefix_count == 2) {
		prefix = "Mi";
	} else if(prefix_count == 3) {
		prefix = "Gi";
	} else if(prefix_count == 4) {
		prefix = "Ti";
	} else if(prefix_count == 5) {
		prefix = "Pi";
	} else if(prefix_count == 6) {
		prefix = "Ei";
	}

	sprintf(result, "%7.2f %sB", size, prefix);
}


int DVDDisplayInfo(dvd_reader_t* dvd, char* device) {
	int i, f;
	int chapters;
	int channels;
	int titles;
	char title_name[33] = "";
	char size[40] = "";
	title_set_info_t* title_set_info = NULL;
	titles_info_t* titles_info = NULL;

	titles_info = DVDGetInfo(dvd);
	if (!titles_info) {
		XLog0(pApp, _("Guesswork of main feature film failed."));
		return(1);
	}

	title_set_info = DVDGetFileSet(dvd);
	if (!title_set_info) {
		DVDFreeTitlesInfo(titles_info);
		return(1);
	}

	DVDGetTitleName(device, title_name);


	XLog2(pApp, _("DVD-Video information of the DVD with title \"%s\""), title_name);

	/* Print file structure */

	XLog2(pApp, _("File Structure DVD"));
	XLog2(pApp, "VIDEO_TS/");
	format_filesize(title_set_info->title_set[0].size_ifo, size);
	XLog2(pApp, "\tVIDEO_TS.IFO\t%10jd\t%s", (intmax_t)title_set_info->title_set[0].size_ifo, size);

	if (title_set_info->title_set[0].size_menu != 0 ) {
		format_filesize(title_set_info->title_set[0].size_menu, size);
		XLog2(pApp, "\tVIDEO_TS.VOB\t%10jd\t%s", (intmax_t)title_set_info->title_set[0].size_menu, size);
	}

	for(i = 0 ; i <= title_set_info->number_of_title_sets; i++) {
		format_filesize(title_set_info->title_set[i].size_ifo, size);
		XLog2(pApp, "\tVTS_%02i_0.IFO\t%10jd\t%s", i, (intmax_t)title_set_info->title_set[i].size_ifo, size);
		if(title_set_info->title_set[i].size_menu != 0) {
			format_filesize(title_set_info->title_set[i].size_menu, size);
			XLog2(pApp, "\tVTS_%02i_0.VOB\t%10jd\t%s", i, (intmax_t)title_set_info->title_set[i].size_menu, size);
		}
		if(title_set_info->title_set[i].number_of_vob_files != 0) {
			for(f = 0; f < title_set_info->title_set[i].number_of_vob_files; f++) {
				format_filesize(title_set_info->title_set[i].size_vob[f], size);
				XLog2(pApp, "\tVTS_%02i_%i.VOB\t%10jd\t%s", i, f + 1, (intmax_t)title_set_info->title_set[i].size_vob[f], size);
			}
		}
	}

	XLog2(pApp, _("Main feature:"));
	XLog2(pApp, _("\tTitle set containing the main feature is %d"), titles_info->main_title_set);
	for (i=0; i < titles_info->number_of_titles ; i++ ) {
		if (titles_info->titles[i].title_set == titles_info->main_title_set) {
			if(titles_info->titles[i].aspect_ratio == 3) {
				XLog2(pApp, _("\tThe aspect ratio of the main feature is 16:9"));
			} else if (titles_info->titles[i].aspect_ratio == 0) {
				XLog2(pApp, _("\tThe aspect ratio of the main feature is 4:3"));
			} else {
				XLog2(pApp, _("\tThe aspect ratio of the main feature is unknown"));
			}

			XLog2(pApp, ngettext("\tThe main feature has %d angle",
					"\tThe main feature has %d angles",
					titles_info->titles[i].angles), titles_info->titles[i].angles);
			XLog2(pApp, ngettext("\tThe main feature has %d audio track",
					"\tThe main feature has %d audio tracks",
					titles_info->titles[i].audio_tracks), titles_info->titles[i].audio_tracks);
			XLog2(pApp, ngettext("\tThe main feature has %d subpicture channel",
					"\tThe main feature has %d subpicture channels",
					titles_info->titles[i].sub_pictures), titles_info->titles[i].sub_pictures);
			chapters=0;
			channels=0;

			for (f=0; f < titles_info->number_of_titles ; f++ ) {
				if ( titles_info->titles[i].title_set == titles_info->main_title_set ) {
					if(chapters < titles_info->titles[f].chapters) {
						chapters = titles_info->titles[f].chapters;
					}
					if(channels < titles_info->titles[f].audio_channels) {
						channels = titles_info->titles[f].audio_channels;
					}
				}
			}
			XLog2(pApp, ngettext("\tThe main feature has a maximum of %d chapter in one of its titles",
					"\tThe main feature has a maximum of %d chapters in one of its titles",
					chapters), chapters);
			XLog2(pApp, ngettext("\tThe main feature has a maximum of %d audio channel in one of its titles",
					"\tThe main feature has a maximum of %d audio channels in one of its titles",
					channels), channels);
			break;
		}
	}

	XLog2(pApp, _("Title Sets:"));
	for (f=0; f < title_set_info->number_of_title_sets ; f++ ) {
		XLog2(pApp, _("\tTitle set %d"), f + 1);
		for (i=0; i < titles_info->number_of_titles ; i++ ) {
			if (titles_info->titles[i].title_set == f + 1) {
				if(titles_info->titles[i].aspect_ratio == 3) {
					XLog2(pApp, _("\t\tThe aspect ratio of title set %d is 16:9"), f + 1);
				} else if (titles_info->titles[i].aspect_ratio == 0) {
					XLog2(pApp, _("\t\tThe aspect ratio of title set %d is 4:3"), f + 1);
				} else {
					XLog2(pApp, _("\t\tThe aspect ratio of title set %d is unknown"), f + 1);
				}
				XLog2(pApp, ngettext("\t\tTitle set %d has %d angle",
						"\t\tTitle set %d has %d angles",
						titles_info->titles[i].angles), f + 1, titles_info->titles[i].angles);
				XLog2(pApp, ngettext("\t\tTitle set %d has %d audio track",
						"\t\tTitle set %d has %d audio tracks",
						titles_info->titles[i].audio_tracks), f + 1, titles_info->titles[i].audio_tracks);
				XLog2(pApp, ngettext("\t\tTitle set %d has %d subpicture channel",
						"\t\tTitle set %d has %d subpicture channels",
						titles_info->titles[i].sub_pictures), f + 1, titles_info->titles[i].sub_pictures);
				break;
			}
		}

		titles = 0;
		for(i = 0; i < titles_info->number_of_titles; i++) {
			if (titles_info->titles[i].title_set == f + 1) {
				titles++;
			}
		}
		/* TODO: this won't work, need tmpbuf */
		XLog2(pApp, ngettext("\t\tTitle included in title set %d is",
				"\t\tTitles included in title set %d are",
				titles), f + 1);

		for (i=0; i < titles_info->number_of_titles ; i++ ) {
			if (titles_info->titles[i].title_set == f + 1) {
				XLog2(pApp, _("\t\t\tTitle %d:"), i + 1);
				XLog2(pApp, ngettext("\t\t\t\tTitle %d has %d chapter",
						"\t\t\t\tTitle %d has %d chapters",
						titles_info->titles[i].chapters), i + 1, titles_info->titles[i].chapters);
				XLog2(pApp, ngettext("\t\t\t\tTitle %d has %d audio channel",
						"\t\t\t\tTitle %d has %d audio channels",
						titles_info->titles[i].audio_channels), i + 1, titles_info->titles[i].audio_channels);
			}
		}
	}
	DVDFreeTitlesInfo(titles_info);
	DVDFreeTitleSetInfo(title_set_info);

	return(0);
}
