//
// Created by F1 on 3/23/2016.
//

#include "Engine.h"


Engine::Engine(struct android_app* mApp)
{
	app = mApp;

	app->userData = this;
	app->onAppCmd = Engine::handle_cmd;
	app->onInputEvent = Engine::handle_input;

	//Setting up accelerometer stuff
	sensor_manager = ASensorManager_getInstance();
	accelerometer_sensor = ASensorManager_getDefaultSensor(sensor_manager,ASENSOR_TYPE_ACCELEROMETER);
	sensor_event_queue = ASensorManager_createEventQueue(sensor_manager, mApp->looper, LOOPER_ID_USER,NULL,NULL);

	//Restore from a saved state
	if(mApp->savedState != NULL)
	{
		//Really not sure when this is running, I'll add debug code so I can be informed when
		LOGE("NOT AN ERROR: notifying that previous saved state was not null, set state to prev state.\n");
		state = *(struct saved_state*) mApp->savedState;
	}
}

void Engine::handle_cmd(struct android_app *app, int32_t cmd)
{
	Engine* eng = (Engine*)app->userData;

	switch(cmd)
	{
		//Saving our current state
		case APP_CMD_SAVE_STATE:
			eng->app->savedState = malloc(sizeof(struct saved_state));
			//*((struct saved_state*) eng->app->savedState) = *(eng->state);
			*((struct saved_state*) eng->app->savedState) = (eng->state);

			eng->app->savedStateSize = sizeof(struct saved_state);
			break;
			//Window is being shown
		case APP_CMD_INIT_WINDOW:
			if(eng->app->window != NULL)
			{
				if(!eng->data_initialized)
				{
					LOGE("attempted to draw screen before loading data.\n");
					if(!eng->init_data())
					{
						return;
					}
				}
				if(eng->init_display() == -1)
				{
					LOGE("Init display returns -1");
					return;
				}
				eng->draw_frame();
			}
			break;
			//Window is being hidden
		case APP_CMD_TERM_WINDOW:
			eng->term_display();
			break;
			//App gained focus, start monitoring accelerometer
		case APP_CMD_GAINED_FOCUS:
			if(eng->accelerometer_sensor != NULL)
			{
				ASensorEventQueue_enableSensor(eng->sensor_event_queue,eng->accelerometer_sensor);
				//60 samples per second
				ASensorEventQueue_setEventRate(eng->sensor_event_queue,eng->accelerometer_sensor, (1000L/60)*1000);
			}
			break;
			//App lost focus, stop monitoring acceleromter, and stop animating
		case APP_CMD_LOST_FOCUS:
			if(eng->accelerometer_sensor != NULL)
			{
				ASensorEventQueue_disableSensor(eng->sensor_event_queue,eng->accelerometer_sensor);
			}
			eng->animating = 0;
			eng->draw_frame();
			break;
		default:
			break;
	}
	return;
}
int32_t Engine::handle_input(struct android_app *app, AInputEvent *event)
{
	Engine* eng = (Engine*)app->userData;

	if(AInputEvent_getType(event)== AINPUT_EVENT_TYPE_MOTION)
	{
		eng->animating = 1;
		//eng->state->x = (int32_t)AMotionEvent_getX(event,0);
		//eng->state->y = (int32_t)AMotionEvent_getY(event,0);
		eng->state.x = (int32_t)AMotionEvent_getX(event,0);
		eng->state.y = (int32_t)AMotionEvent_getY(event,0);
		return 1;
	}
	return 0;
}

int Engine::init_display()
{
#ifdef DEBUG_MODE
	LOGI("LIFECYCLE: Display initialized.\n");
#endif
	//Setting up OPENGLES
	const EGLint config_attribs[] = {
		   EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		   EGL_BLUE_SIZE, 8,
		   EGL_GREEN_SIZE, 8,
		   EGL_RED_SIZE, 8,
		   EGL_NONE
	};
	EGLint w,h,num_configs;
	EGLConfig config;
	EGLSurface surface;
	EGLContext context;

	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	//Should we check for this?
	/*if(display == NULL)
	{
		LOGW("eglGetDisplay returned NULL");
		return -1;
	}*/

	eglInitialize(display,0,0);

	//Choosing first config that matches our criteria
	eglChooseConfig(display, config_attribs, &config, 1, &num_configs);
	surface = eglCreateWindowSurface(display, config, app->window, NULL);

	const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

	context = eglCreateContext(display, config, NULL, context_attribs);

	if(eglMakeCurrent(display, surface, surface, context) == EGL_FALSE)
	{
		//error, should return -1 or so
		LOGW("eglMakeCurrent Failed");
		return -1;
	}

	eglQuerySurface(display,surface,EGL_WIDTH, &w);
	eglQuerySurface(display,surface,EGL_HEIGHT, &h);

	egl_display = display;
	egl_context = context;
	egl_surface = surface;
	width = w;
	height = h;
	//state->angle = 0;
	state.angle = 0;

	animating = 1;

	//====== GL Initialization code ======
	if(!init_gl())
		return -1;

	return 0;
}

//========================================= Loading assets ======================================

int Engine::load_shaders()
{
	vert_shader_name = "minimal.vert";
	frag_shader_name = "minimal.frag";
	frag_shader_src = File_Utils::load_asset("minimal.frag");
	vert_shader_src = File_Utils::load_asset("minimal.vert");

	return 1;
}



//Returns 0 on fail, returns 1 on success.
int Engine::load_assets()
{
	//Loading the test texture.
	const char* tex_file = File_Utils::load_asset("tex.pkm");
	test_tex = (GLuint) tex_file;
	return 1;
}

//=================================================================================================

//========================================= Unloading assets ======================================
void Engine::unload_shaders()
{
	if(vert_shader_src)
		free((char*)vert_shader_src);
	if(frag_shader_src)
		free((char*)frag_shader_src);
}

void Engine::unload_assets()
{
	if(test_tex)
		free((char*)test_tex);
}
//=================================================================================================

void Engine::term_display()
{
#ifdef DEBUG_MODE
	LOGI("LIFECYCLE: DISPLAY TERMINATED\n");
#endif

	//====== GL termination code ======
	term_gl();
	//=================================
	if(egl_display != EGL_NO_DISPLAY)
	{
		eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if(egl_context != EGL_NO_CONTEXT)
		{
			eglDestroyContext(egl_display, egl_context);
		}
		if(egl_surface != EGL_NO_SURFACE)
		{
			eglDestroySurface(egl_display, egl_surface);
		}
		eglTerminate(egl_display);
	}
	animating = 0;
	egl_display = EGL_NO_DISPLAY;
	egl_context = EGL_NO_CONTEXT;
	egl_surface = EGL_NO_SURFACE;

}

int Engine::init_sl()
{
	//What variables do we need to hold?
	//How are we going to hold this data?
}

void Engine::term_sl()
{
	//What variables do I need to free?
}

int Engine::init_gl()
{
	//Init gl state
	//At this stage, all of the shaders have already been loaded.
	//glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	glEnable(GL_CULL_FACE);
	//glDisable(GL_DEPTH_TEST);


	//enable blend function
	glEnable(GL_BLEND);
	//set blend function
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	gl_program = glCreateProgram();
	LOGI("gl program is: %d.\n", gl_program);
	if(!gl_program)
	{
		LOGE("Error: failed to create gl program.\n");
		return 0;
	}

	test_vert_shader = GL_Utils::load_shader(vert_shader_src,vert_shader_name,GL_VERTEX_SHADER);
	LOGI("vertex shader: %d\n",(int) test_vert_shader);

	test_frag_shader = GL_Utils::load_shader(frag_shader_src,frag_shader_name,GL_FRAGMENT_SHADER);
	LOGI("fragment shader: %d\n",(int) test_frag_shader);

	glAttachShader(gl_program, test_vert_shader);
	glAttachShader(gl_program, test_frag_shader);

	glLinkProgram(gl_program);

	GLint linked;
	glGetProgramiv(gl_program, GL_LINK_STATUS, &linked);

	if(linked == GL_FALSE)
	{
		LOGE("Error: Failed to link gl program.\n");

		GLint log_size = 0;
		glGetProgramiv(gl_program, GL_INFO_LOG_LENGTH, &log_size);

		GLchar *info_log = (GLchar *) malloc(sizeof(GLchar) * log_size);
		glGetProgramInfoLog(gl_program, log_size, &log_size, info_log);
		LOGE("   linker log: %s.\n", info_log);
		free(info_log);

		glDeleteProgram(gl_program);
		gl_program = 0;
		return 0;
	}


	glUseProgram(gl_program);

	shader_fill_color_loc = glGetAttribLocation(gl_program, "fill_color");
	shader_vert_pos_loc = glGetAttribLocation(gl_program, "vert_pos");
	shader_uv_loc = glGetAttribLocation(gl_program, "src_tex_coord");
	shader_tex_loc = glGetUniformLocation(gl_program, "tex");

	//==================================== Loading texture ===================
	GLuint tex_id;
	glGenTextures(1, &tex_id);
	//======================
	//glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex_id);
	LOGI("Texture id before binding: %u\n", tex_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glCompressedTexImage2D(GL_TEXTURE_2D, 0, ETC1_RGB8, 512, 512, 0, 131072, (const void *) test_tex);
	glBindTexture(GL_TEXTURE_2D,0);


	LOGI("Texture id after binding: %u\n", tex_id);
	texture_id = tex_id;
	//========================================================================
	glViewport(0, 0, width, height);
	glClearColor(1, 1, 1, 1);
	return 1;
}
void Engine::term_gl()
{
	glDeleteTextures(1, &texture_id);

	glDeleteProgram(gl_program);


	//Unloading all loaded shaders
	if(test_frag_shader)
		GL_Utils::unload_shader(test_frag_shader);
	if(test_vert_shader)
		GL_Utils::unload_shader(test_vert_shader);

	gl_program = 0;
	shader_vert_pos_loc = -1;
	shader_fill_color_loc = -1;
	shader_uv_loc = -1;
	shader_tex_loc = -1;
	texture_id = 0;
	test_frag_shader = 0;
	test_vert_shader = 0;
}

int Engine::init_data()
{
	LOGI("init_data...\n");
	if(!load_shaders())
		return 0;
	if(!load_assets())
		return 0;

	data_initialized = true;
	return 1;
}

void Engine::term_data()
{
	unload_shaders();
	unload_assets();
	data_initialized = false;
}

void Engine::draw_frame()
{
	//Need to initialize data before the screen context has been created.
	if(!data_initialized)
	{
		if(!init_data())
			return;
	}
#ifdef DEBUG_MODE
	static bool was_rendering = true;
	if(!egl_display)
	{
		if(was_rendering)
		{
			LOGI("LIFECYCLE: RENDER ABORTED, display not initialized\n");
			was_rendering = false;
		}
		return;
	}
	if(!was_rendering)
	{
		LOGI("LIFECYCLE: rendering...\n");
		was_rendering = true;
	}
#endif

	if(!egl_display)
	{
		return;
	}

	const float triangleVertices[] =
	{
		-0.5f, 0.5f, 0.0f,
		0.0f, -0.5f, 0.0f,
		0.5f, 0.5f, 0.0f,
	};
	const float triangleUVs[] =
	{
		0.25f, 0.0f,
		0.5f, 1.0f,
		0.75f, 0.0f,
	};
	const float triangleColors[] =
	{
		1.0, 0.0, 0.0, 1.0,
		0.0, 1.0, 0.0, 1.0,
		0.0, 0.0, 1.0, 1.0,
	};


	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glUseProgram(gl_program);

	//Filling the screen with a color
	glClearColor(((float)state.x)/width, 0.0f/*state.angle*/, ((float)state.y)/height,1);
	//glClear(GL_COLOR_BUFFER_BIT);

	glVertexAttribPointer(shader_vert_pos_loc, 3, GL_FLOAT, GL_FALSE, 0, triangleVertices);
	glEnableVertexAttribArray(shader_vert_pos_loc);

	//Pass the fill color info
	if(shader_fill_color_loc != -1)
	{
		glVertexAttribPointer(shader_fill_color_loc, 4, GL_FLOAT, GL_FALSE, 0, triangleColors);
		glEnableVertexAttribArray(shader_fill_color_loc);
	}
	//Pass the uv coords
	glVertexAttribPointer(shader_uv_loc,2, GL_FLOAT, GL_FALSE, 0, triangleUVs);
	glEnableVertexAttribArray(shader_uv_loc);
	//Binding the texture to use
	if(texture_id)
	{
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture_id);
		glUniform1i(shader_tex_loc, 0);
	}
	else
		LOGE("texture not set, abort binding tex.\n");

	glDrawArrays(GL_TRIANGLES, 0, 3);

	eglSwapBuffers(egl_display, egl_surface);
}
