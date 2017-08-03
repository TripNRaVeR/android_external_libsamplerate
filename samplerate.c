/*
** Copyright (c) 2002-2016, Erik de Castro Lopo <erikd@mega-nerd.com>
** All rights reserved.
**
** This code is released under 2-clause BSD license. Please see the
** file at : https://github.com/erikd/libsamplerate/blob/master/COPYING
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"config.h"

#include	"include/samplerate.h"
#include	"include/float_cast.h"
#include	"common.h"

static int psrc_set_converter (SRC_PRIVATE	*psrc, int converter_type) ;


SRC_STATE *
src_new (int converter_type, int channels, int *error)
{	SRC_PRIVATE	*psrc ;

	if (error)
		*error = SRC_ERR_NO_ERROR ;

	if (channels < 1)
	{	if (error)
			*error = SRC_ERR_BAD_CHANNEL_COUNT ;
		return NULL ;
		} ;

	if ((psrc = calloc (1, sizeof (*psrc))) == NULL)
	{	if (error)
			*error = SRC_ERR_MALLOC_FAILED ;
		return NULL ;
		} ;

	psrc->channels = channels ;
	psrc->mode = SRC_MODE_PROCESS ;

	if (psrc_set_converter (psrc, converter_type) != SRC_ERR_NO_ERROR)
	{	if (error)
			*error = SRC_ERR_BAD_CONVERTER ;
		free (psrc) ;
		psrc = NULL ;
		} ;

	src_reset ((SRC_STATE*) psrc) ;

	return (SRC_STATE*) psrc ;
} /* src_new */

SRC_STATE*
src_callback_new (src_callback_t func, int converter_type, int channels, int *error, void* cb_data)
{	SRC_STATE	*src_state ;

	if (func == NULL)
	{	if (error)
			*error = SRC_ERR_BAD_CALLBACK ;
		return NULL ;
		} ;

	if (error != NULL)
		*error = 0 ;

	if ((src_state = src_new (converter_type, channels, error)) == NULL)
		return NULL ;

	src_reset (src_state) ;

	((SRC_PRIVATE*) src_state)->mode = SRC_MODE_CALLBACK ;
	((SRC_PRIVATE*) src_state)->callback_func = func ;
	((SRC_PRIVATE*) src_state)->user_callback_data = cb_data ;

	return src_state ;
} /* src_callback_new */

SRC_STATE *
src_delete (SRC_STATE *state)
{	SRC_PRIVATE *psrc ;

	psrc = (SRC_PRIVATE*) state ;
	if (psrc)
	{	if (psrc->private_data)
			free (psrc->private_data) ;
		memset (psrc, 0, sizeof (SRC_PRIVATE)) ;
		free (psrc) ;
		} ;

	return NULL ;
} /* src_state */

int
src_process (SRC_STATE *state, SRC_DATA *data)
{	SRC_PRIVATE *psrc ;
	int error ;

	psrc = (SRC_PRIVATE*) state ;

	if (psrc == NULL)
		return SRC_ERR_BAD_STATE ;
	if (psrc->vari_process == NULL || psrc->const_process == NULL)
		return SRC_ERR_BAD_PROC_PTR ;

	if (psrc->mode != SRC_MODE_PROCESS)
		return SRC_ERR_BAD_MODE ;

	/* Check for valid SRC_DATA first. */
	if (data == NULL)
		return SRC_ERR_BAD_DATA ;

	/* And that data_in and data_out are valid. */
	if (data->data_in == NULL || data->data_out == NULL)
		return SRC_ERR_BAD_DATA_PTR ;

	/* Check src_ratio is in range. */
	if (is_bad_src_ratio (data->src_ratio))
		return SRC_ERR_BAD_SRC_RATIO ;

	if (data->input_frames < 0)
		data->input_frames = 0 ;
	if (data->output_frames < 0)
		data->output_frames = 0 ;

	if (data->data_in < data->data_out)
	{	if (data->data_in + data->input_frames * psrc->channels > data->data_out)
		{	/*-printf ("\n\ndata_in: %p    data_out: %p\n",
				(void*) (data->data_in + data->input_frames * psrc->channels), (void*) data->data_out) ;-*/
			return SRC_ERR_DATA_OVERLAP ;
			} ;
		}
	else if (data->data_out + data->output_frames * psrc->channels > data->data_in)
	{	/*-printf ("\n\ndata_in : %p   ouput frames: %ld    data_out: %p\n", (void*) data->data_in, data->output_frames, (void*) data->data_out) ;

		printf ("data_out: %p (%p)    data_in: %p\n", (void*) data->data_out,
			(void*) (data->data_out + data->input_frames * psrc->channels), (void*) data->data_in) ;-*/
		return SRC_ERR_DATA_OVERLAP ;
		} ;

	/* Set the input and output counts to zero. */
	data->input_frames_used = 0 ;
	data->output_frames_gen = 0 ;

	/* Special case for when last_ratio has not been set. */
	if (psrc->last_ratio < (1.0 / SRC_MAX_RATIO))
		psrc->last_ratio = data->src_ratio ;

	/* Now process. */
	if (fabs (psrc->last_ratio - data->src_ratio) < 1e-15)
		error = psrc->const_process (psrc, data) ;
	else
		error = psrc->vari_process (psrc, data) ;

	return error ;
} /* src_process */

long
src_callback_read (SRC_STATE *state, double src_ratio, long frames, float *data)
{	SRC_PRIVATE	*psrc ;
	SRC_DATA	src_data ;

	long	output_frames_gen ;
	int		error = 0 ;

	if (state == NULL)
		return 0 ;

	if (frames <= 0)
		return 0 ;

	psrc = (SRC_PRIVATE*) state ;

	if (psrc->mode != SRC_MODE_CALLBACK)
	{	psrc->error = SRC_ERR_BAD_MODE ;
		return 0 ;
		} ;

	if (psrc->callback_func == NULL)
	{	psrc->error = SRC_ERR_NULL_CALLBACK ;
		return 0 ;
		} ;

	memset (&src_data, 0, sizeof (src_data)) ;

	/* Check src_ratio is in range. */
	if (is_bad_src_ratio (src_ratio))
	{	psrc->error = SRC_ERR_BAD_SRC_RATIO ;
		return 0 ;
		} ;

	/* Switch modes temporarily. */
	src_data.src_ratio = src_ratio ;
	src_data.data_out = data ;
	src_data.output_frames = frames ;

	src_data.data_in = psrc->saved_data ;
	src_data.input_frames = psrc->saved_frames ;

	output_frames_gen = 0 ;
	while (output_frames_gen < frames)
	{	/*	Use a dummy array for the case where the callback function
		**	returns without setting the ptr.
		*/
		float dummy [1] ;

		if (src_data.input_frames == 0)
		{	float *ptr = dummy ;

			src_data.input_frames = psrc->callback_func (psrc->user_callback_data, &ptr) ;
			src_data.data_in = ptr ;

			if (src_data.input_frames == 0)
				src_data.end_of_input = 1 ;
			} ;

		/*
		** Now call process function. However, we need to set the mode
		** to SRC_MODE_PROCESS first and when we return set it back to
		** SRC_MODE_CALLBACK.
		*/
		psrc->mode = SRC_MODE_PROCESS ;
		error = src_process (state, &src_data) ;
		psrc->mode = SRC_MODE_CALLBACK ;

		if (error != 0)
			break ;

		src_data.data_in += src_data.input_frames_used * psrc->channels ;
		src_data.input_frames -= src_data.input_frames_used ;

		src_data.data_out += src_data.output_frames_gen * psrc->channels ;
		src_data.output_frames -= src_data.output_frames_gen ;

		output_frames_gen += src_data.output_frames_gen ;

		if (src_data.end_of_input == SRC_TRUE && src_data.output_frames_gen == 0)
			break ;
		} ;

	psrc->saved_data = src_data.data_in ;
	psrc->saved_frames = src_data.input_frames ;

	if (error != 0)
	{	psrc->error = error ;
	 	return 0 ;
		} ;

	return output_frames_gen ;
} /* src_callback_read */

/*==========================================================================
*/

int
src_set_ratio (SRC_STATE *state, double new_ratio)
{	SRC_PRIVATE *psrc ;

	psrc = (SRC_PRIVATE*) state ;

	if (psrc == NULL)
		return SRC_ERR_BAD_STATE ;
	if (psrc->vari_process == NULL || psrc->const_process == NULL)
		return SRC_ERR_BAD_PROC_PTR ;

	if (is_bad_src_ratio (new_ratio))
		return SRC_ERR_BAD_SRC_RATIO ;

	psrc->last_ratio = new_ratio ;

	return SRC_ERR_NO_ERROR ;
} /* src_set_ratio */

int
src_get_channels (SRC_STATE *state)
{	SRC_PRIVATE *psrc ;

	psrc = (SRC_PRIVATE*) state ;

	if (psrc == NULL)
		return SRC_ERR_BAD_STATE ;
	if (psrc->vari_process == NULL || psrc->const_process == NULL)
		return SRC_ERR_BAD_PROC_PTR ;

	return psrc->channels ;
} /* src_get_channels */

int
src_reset (SRC_STATE *state)
{	SRC_PRIVATE *psrc ;

	if ((psrc = (SRC_PRIVATE*) state) == NULL)
		return SRC_ERR_BAD_STATE ;

	if (psrc->reset != NULL)
		psrc->reset (psrc) ;

	psrc->last_position = 0.0 ;
	psrc->last_ratio = 0.0 ;

	psrc->saved_data = NULL ;
	psrc->saved_frames = 0 ;

	psrc->error = SRC_ERR_NO_ERROR ;

	return SRC_ERR_NO_ERROR ;
} /* src_reset */

int
src_is_valid_ratio (double ratio)
{
	if (is_bad_src_ratio (ratio))
		return SRC_FALSE ;

	return SRC_TRUE ;
} /* src_is_valid_ratio */

/*==============================================================================
**	Simple interface for performing a single conversion from input buffer to
**	output buffer at a fixed conversion ratio.
*/

int
src_simple (SRC_DATA *src_data, int converter, int channels)
{	SRC_STATE	*src_state ;
	int 		error ;

	if ((src_state = src_new (converter, channels, &error)) == NULL)
		return error ;

	src_data->end_of_input = 1 ; /* Only one buffer worth of input. */

	error = src_process (src_state, src_data) ;

	src_delete (src_state) ;

	return error ;
} /* src_simple */

/*==============================================================================
**	Private functions.
*/

static int
psrc_set_converter (SRC_PRIVATE	*psrc, int converter_type)
{
	if (sinc_set_converter (psrc, converter_type) == SRC_ERR_NO_ERROR)
		return SRC_ERR_NO_ERROR ;

	if (zoh_set_converter (psrc, converter_type) == SRC_ERR_NO_ERROR)
		return SRC_ERR_NO_ERROR ;

	if (linear_set_converter (psrc, converter_type) == SRC_ERR_NO_ERROR)
		return SRC_ERR_NO_ERROR ;

	return SRC_ERR_BAD_CONVERTER ;
} /* psrc_set_converter */

