#include "ImageProc.h"

int errnoexit(const char *s) {
	LOGE("%s error %d, %s", s, errno, strerror(errno));
	return ERROR_LOCAL;
}

int xioctl(int fd, int request, void *arg) {
	int r;

	do
		r = ioctl(fd, request, arg);
	while (-1 == r && EINTR == errno);

	return r;
}
int checkCamerabase(void) {
	struct stat st;
	int i;
	int start_from_4 = 1;

	/* if /dev/video[0-3] exist, camerabase=4, otherwise, camrerabase = 0 */
	for (i = 0; i < 4; i++) {
		sprintf(dev_name, "/dev/video%d", i);
		if (-1 == stat(dev_name, &st)) {
			start_from_4 &= 0;
		} else {
			start_from_4 &= 1;
		}
	}

	if (start_from_4) {
		return 4;
	} else {
		return 0;
	}
}

int opendevice(int i) {
	LOGE("opendevice");
	struct stat st;

	sprintf(dev_name, "/dev/video%d", i);

	if (-1 == stat(dev_name, &st)) {
		LOGE("Cannot identify '%s': %d, %s", dev_name, errno, strerror(errno));
		return ERROR_LOCAL;
	}

	if (!S_ISCHR(st.st_mode)) {
		LOGE("%s is no device", dev_name);
		return ERROR_LOCAL;
	}

	fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);

	if (-1 == fd) {
		LOGE("Cannot open '%s': %d, %s", dev_name, errno, strerror(errno));
		return ERROR_LOCAL;
	}
	return SUCCESS_LOCAL;
}

int initdevice(void) {
	LOGE("initdevice");
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	struct v4l2_format fmt_set;
	unsigned int min;
	struct v4l2_fmtdesc fmt_des;
	int ret_des;
	int index = 0;

	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			LOGE("%s is no V4L2 device", dev_name);
			return ERROR_LOCAL;
		} else {
			return errnoexit("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		LOGE("%s is no video capture device", dev_name);
		return ERROR_LOCAL;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		LOGE("%s does not support streaming i/o", dev_name);
		return ERROR_LOCAL;
	}

	CLEAR(cropcap);

	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect;

		if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
			switch (errno) {
			case EINVAL:
				break;
			default:
				break;
			}
		}
	}

	CLEAR(fmt_des);
	fmt_des.index = 0;
	fmt_des.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	do {
		ret_des = xioctl(fd, VIDIOC_ENUM_FMT, &fmt_des);
		if (ret_des < 0) {
			LOGE("FMT type max number = %d", fmt_des.index);
			break;
		}
		LOGE("FMT type = %d", fmt_des.pixelformat);
		fmt_des.index++;
	} while (fmt_des.index < 10);

	CLEAR(fmt);

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	// [liuchang start] get camera information
	if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt)) {
		return errnoexit("VIDIOC_S_FMT1： get VIDIOC FMT information error");
	}

	LOGE("width = %d", fmt.fmt.pix.width);
	LOGE("height = %d", fmt.fmt.pix.height);
	LOGE("pixelformat = %d", fmt.fmt.pix.pixelformat);
	LOGE("field = %d", fmt.fmt.pix.field);
	LOGE("sizeimage = %d", fmt.fmt.pix.sizeimage);

	CLEAR(fmt_set);

	fmt_set.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	LOGE("=================setting camera fmt parameters===============");
	fmt_set.fmt.pix.width = fmt.fmt.pix.width;
	fmt_set.fmt.pix.height = fmt.fmt.pix.height;

	fmt_set.fmt.pix.pixelformat = fmt.fmt.pix.pixelformat;
	fmt_set.fmt.pix.field = fmt.fmt.pix.field; // 天敏摄像头参数为1

	LOGE("pixelformat = %d", fmt_set.fmt.pix.pixelformat);
	LOGE("field = %d", fmt_set.fmt.pix.field);

	if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt_set))
		return errnoexit("VIDIOC_S_FMT");

	min = fmt_set.fmt.pix.width * 2;
	if (fmt_set.fmt.pix.bytesperline < min)
		fmt_set.fmt.pix.bytesperline = min;
	min = fmt_set.fmt.pix.bytesperline * fmt_set.fmt.pix.height;
	if (fmt_set.fmt.pix.sizeimage < min)
		fmt_set.fmt.pix.sizeimage = min;

	cap_image_size = fmt_set.fmt.pix.sizeimage;

	return inituserptr(fmt_set.fmt.pix.sizeimage);

}

int initmmap(void) {
	LOGE("initmmap");
	struct v4l2_requestbuffers req;
	void* myBuf = NULL;
	int retValue = 0;
	int framesize = 640 * 480 * 3 / 2;

	CLEAR(req);

	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	//	req.memory              = V4L2_MEMORY_MMAP;
	req.memory = V4L2_MEMORY_USERPTR;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			LOGE("%s does not support memory mapping", dev_name);
			return ERROR_LOCAL;
		} else {
			return errnoexit("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		LOGE("Insufficient buffer memory on %s", dev_name);
		return ERROR_LOCAL;
	}

	buffers = calloc(req.count, sizeof(*buffers));
	myBuf = calloc(req.count, framesize);
	LOGE("req.count = %d", req.count);

	if (!buffers) {
		LOGE("Out of memory");
		return ERROR_LOCAL;
	}

	if (!myBuf) {
		LOGE("Out of memory");
		return ERROR_LOCAL;
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		buffers[n_buffers].length = framesize;
		buffers[n_buffers].start = myBuf + n_buffers * framesize;
		LOGE("buffers[%d].length = %d", n_buffers, buffers[n_buffers].length);
		LOGE("buffers[%d].start = %d", n_buffers, buffers[n_buffers].start);
	}

	/*
	 for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
	 struct v4l2_buffer buf;

	 CLEAR (buf);

	 buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	 //	buf.memory      = V4L2_MEMORY_MMAP;
	 buf.memory = V4L2_MEMORY_USERPTR;
	 buf.index       = n_buffers;

	 retValue = xioctl (fd, VIDIOC_QUERYBUF, &buf);
	 if (-1 == retValue)
	 return errnoexit ("VIDIOC_QUERYBUF");

	 LOGE("xioctl's return value is %d", retValue);

	 buffers[n_buffers].length = buf.length;
	 buffers[n_buffers].start =
	 mmap (NULL ,
	 buf.length,
	 PROT_READ | PROT_WRITE,
	 MAP_SHARED,
	 fd, buf.m.offset);

	 LOGE("mmap's parameters buf.length = %d", buf.length);
	 LOGE("mmap's return value is %d", buffers[n_buffers].start);

	 if (MAP_FAILED == buffers[n_buffers].start){
	 LOGE("calling mmap failed!");
	 return errnoexit ("mmap");
	 }
	 }
	 */

	return SUCCESS_LOCAL;
}

int inituserptr(unsigned int buffer_size) {
	LOGE("initmmap");
	struct v4l2_requestbuffers req;
	unsigned int page_size;
	page_size = getpagesize();
	buffer_size = (buffer_size + page_size - 1) & ~(page_size - 1);
	CLEAR(req);
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;
	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support "
				"user pointer i/o\n", dev_name);
			exit(EXIT_FAILURE);
		} else {
			errnoexit("VIDIOC_REQBUFS");
		}
	}
	buffers = calloc(4, sizeof(*buffers));
	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
	for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
		buffers[n_buffers].length = buffer_size;
		buffers[n_buffers].start = memalign(/* boundary */page_size,
				buffer_size);
		if (!buffers[n_buffers].start) {
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}
	}

	return SUCCESS_LOCAL;
}

/* Modify to QBUF request, mainly changes parameters */
int startcapturing(void) {
	LOGE("startcapturing");
	unsigned int i;
	enum v4l2_buf_type type;

	for (i = 0; i < n_buffers; ++i) {
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		//buf.memory      = V4L2_MEMORY_MMAP;
		buf.memory = V4L2_MEMORY_USERPTR;
		buf.index = i;
		buf.m.userptr = (unsigned long) buffers[i].start;
		buf.length = buffers[i].length;

		if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
			return errnoexit("VIDIOC_QBUF");
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
		return errnoexit("VIDIOC_STREAMON");

	return SUCCESS_LOCAL;
}

int readframeonce(void) {
	LOGE("readframeonce");
	for (;;) {
		LOGE("readframonce in the for loop");
		fd_set fds;
		struct timeval tv;
		int r;

		LOGE("readframe before FD_ZERO");
		FD_ZERO(&fds);
		LOGE("readframe after FD_ZERO");
		FD_SET(fd, &fds);
		LOGE("readframe after FD_SET");
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		LOGE("readframe before select");
		LOGE("fd = %d", fd);
		//	r = select (fd, &fds, NULL, NULL, &tv);
		r = select(fd + 1, &fds, NULL, NULL, &tv);
		LOGE("readframeonce after select");

		r = 2;

		if (-1 == r) {
			if (EINTR == errno)
				continue;

			return errnoexit("select");
		}

		if (0 == r) {
			LOGE("select timeout");
			return ERROR_LOCAL;

		}

		LOGE("before readrframe");
		if (readframe() == 1) {
			LOGE("break from readframeonce");
			break;
		}
		LOGE("readframeonce is running");
	}

	return SUCCESS_LOCAL;

}

void processimage(const void *p) {
	LOGE("processimage");
	yuyv422toABGRY((unsigned char *) p);
}

int readframe(void) {
	LOGE("readframe");
	struct v4l2_buffer buf;
	unsigned int i;

	CLEAR(buf);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	//buf.memory = V4L2_MEMORY_MMAP;
	buf.memory = V4L2_MEMORY_USERPTR;

	LOGE("before ioctrl");

	if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
		switch (errno) {
		case EAGAIN:
			return 0;
		case EIO:
		default:
			return errnoexit("VIDIOC_DQBUF");
		}
	}

	for (i = 0; i < n_buffers; ++i)
		if (buf.m.userptr == (unsigned long) buffers[i].start && buf.length
				== buffers[i].length)
			break;
	assert(i < n_buffers);
	//      printf("length = %d\r", buffers[i].length);
	//      process_image((void *) buf.m.userptr, buffers[i].length);
	printf("image_size = %d,\t IO_METHOD_USERPTR buffer.length=%d\r",
			cap_image_size, buffers[0].length);

	processimage(buffers[buf.index].start);
	LOGE("after processimage");

	if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
		return errnoexit("VIDIOC_QBUF");

	return 1;
}

int stopcapturing(void) {
	LOGE("stopcapturing");
	enum v4l2_buf_type type;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
		return errnoexit("VIDIOC_STREAMOFF");

	return SUCCESS_LOCAL;

}

int uninitdevice(void) {
	LOGE("uninitdevice");
	unsigned int i;

	//	for (i = 0; i < n_buffers; ++i)
	//		if (-1 == munmap(buffers[i].start, buffers[i].length))
	//			return errnoexit("munmap");

	for (i = 0; i < n_buffers; ++i)
		free(buffers[i].start);

	free(buffers);
	return SUCCESS_LOCAL;
}

int closedevice(void) {
	LOGE("closedevice");
	if (-1 == close(fd)) {
		fd = -1;
		return errnoexit("close");
	}

	fd = -1;
	return SUCCESS_LOCAL;
}

void yuyv422toABGRY(unsigned char *src) {
	LOGE("yuyv422toABGRY");
	int width = 0;
	int height = 0;

	width = IMG_WIDTH;
	height = IMG_HEIGHT;

	int frameSize = width * height * 2;

	int i;

	if ((!rgb || !ybuf)) {
		return;
	}
	int *lrgb = NULL;
	int *lybuf = NULL;

	lrgb = &rgb[0];
	lybuf = &ybuf[0];

	if (yuv_tbl_ready == 0) {
		for (i = 0; i < 256; i++) {
			y1192_tbl[i] = 1192 * (i - 16);
			if (y1192_tbl[i] < 0) {
				y1192_tbl[i] = 0;
			}

			v1634_tbl[i] = 1634 * (i - 128);
			v833_tbl[i] = 833 * (i - 128);
			u400_tbl[i] = 400 * (i - 128);
			u2066_tbl[i] = 2066 * (i - 128);
		}
		yuv_tbl_ready = 1;
	}

	for (i = 0; i < frameSize; i += 4) {
		unsigned char y1, y2, u, v;
		y1 = src[i];
		u = src[i + 1];
		y2 = src[i + 2];
		v = src[i + 3];

		int y1192_1 = y1192_tbl[y1];
		int r1 = (y1192_1 + v1634_tbl[v]) >> 10;
		int g1 = (y1192_1 - v833_tbl[v] - u400_tbl[u]) >> 10;
		int b1 = (y1192_1 + u2066_tbl[u]) >> 10;

		int y1192_2 = y1192_tbl[y2];
		int r2 = (y1192_2 + v1634_tbl[v]) >> 10;
		int g2 = (y1192_2 - v833_tbl[v] - u400_tbl[u]) >> 10;
		int b2 = (y1192_2 + u2066_tbl[u]) >> 10;

		r1 = r1 > 255 ? 255 : r1 < 0 ? 0 : r1;
		g1 = g1 > 255 ? 255 : g1 < 0 ? 0 : g1;
		b1 = b1 > 255 ? 255 : b1 < 0 ? 0 : b1;
		r2 = r2 > 255 ? 255 : r2 < 0 ? 0 : r2;
		g2 = g2 > 255 ? 255 : g2 < 0 ? 0 : g2;
		b2 = b2 > 255 ? 255 : b2 < 0 ? 0 : b2;

		*lrgb++ = 0xff000000 | b1 << 16 | g1 << 8 | r1;
		*lrgb++ = 0xff000000 | b2 << 16 | g2 << 8 | r2;

		if (lybuf != NULL) {
			*lybuf++ = y1;
			*lybuf++ = y2;
		}
	}

}

// [liuchang] convert mjpeg to ABGRY 
void mjpegtoABGRY(unsigned char *src) {

	int width = 0;
	int height = 0;

	width = IMG_WIDTH;
	height = IMG_HEIGHT;

	int frameSize = width * height * 2;

	int i;

	if ((!rgb || !ybuf)) {
		return;
	}
	int *lrgb = NULL;
	int *lybuf = NULL;

	lrgb = &rgb[0];
	lybuf = &ybuf[0];

	if (yuv_tbl_ready == 0) {
		for (i = 0; i < 256; i++) {
			y1192_tbl[i] = 1192 * (i - 16);
			if (y1192_tbl[i] < 0) {
				y1192_tbl[i] = 0;
			}

			v1634_tbl[i] = 1634 * (i - 128);
			v833_tbl[i] = 833 * (i - 128);
			u400_tbl[i] = 400 * (i - 128);
			u2066_tbl[i] = 2066 * (i - 128);
		}
		yuv_tbl_ready = 1;
	}

	for (i = 0; i < frameSize; i += 4) {
		unsigned char y1, y2, u, v;
		y1 = src[i];
		u = src[i + 1];
		y2 = src[i + 2];
		v = src[i + 3];

		int y1192_1 = y1192_tbl[y1];
		int r1 = (y1192_1 + v1634_tbl[v]) >> 10;
		int g1 = (y1192_1 - v833_tbl[v] - u400_tbl[u]) >> 10;
		int b1 = (y1192_1 + u2066_tbl[u]) >> 10;

		int y1192_2 = y1192_tbl[y2];
		int r2 = (y1192_2 + v1634_tbl[v]) >> 10;
		int g2 = (y1192_2 - v833_tbl[v] - u400_tbl[u]) >> 10;
		int b2 = (y1192_2 + u2066_tbl[u]) >> 10;

		r1 = r1 > 255 ? 255 : r1 < 0 ? 0 : r1;
		g1 = g1 > 255 ? 255 : g1 < 0 ? 0 : g1;
		b1 = b1 > 255 ? 255 : b1 < 0 ? 0 : b1;
		r2 = r2 > 255 ? 255 : r2 < 0 ? 0 : r2;
		g2 = g2 > 255 ? 255 : g2 < 0 ? 0 : g2;
		b2 = b2 > 255 ? 255 : b2 < 0 ? 0 : b2;

		*lrgb++ = 0xff000000 | b1 << 16 | g1 << 8 | r1;
		*lrgb++ = 0xff000000 | b2 << 16 | g2 << 8 | r2;

		if (lybuf != NULL) {
			*lybuf++ = y1;
			*lybuf++ = y2;
		}
	}

}

void Java_com_camera_simplewebcam_CameraPreview_pixeltobmp(JNIEnv* env,
		jobject thiz, jobject bitmap) {
	LOGE("Java_com_camera_simplewebcam_CameraPreview_pixeltobmp");
	jboolean bo;

	AndroidBitmapInfo info;
	void* pixels;
	int ret;
	int i;
	int *colors;

	int width = 0;
	int height = 0;

	if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
		LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
		return;
	}

	width = info.width;
	height = info.height;

	if (!rgb || !ybuf)
		return;

	if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
		LOGE("Bitmap format is not RGBA_8888 !");
		return;
	}

	if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
		LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
	}

	colors = (int*) pixels;
	int *lrgb = NULL;
	lrgb = &rgb[0];

	for (i = 0; i < width * height; i++) {
		*colors++ = *lrgb++;
	}

	AndroidBitmap_unlockPixels(env, bitmap);

}

jint Java_com_camera_simplewebcam_CameraPreview_prepareCamera(JNIEnv* env,
		jobject thiz, jint videoid) {
	LOGE("Java_com_camera_simplewebcam_CameraPreview_prepareCamera");
	int ret;

	if (camerabase < 0) {
		camerabase = checkCamerabase();
	}

	ret = opendevice(camerabase + videoid);

	if (ret != ERROR_LOCAL) {
		LOGE("device open successfully!");
	}

	if (ret != ERROR_LOCAL) {
		ret = initdevice();
		if (ret == ERROR_LOCAL) {
			LOGE("device init error");
		}
	}
	if (ret != ERROR_LOCAL) {
		ret = startcapturing();

		if (ret != SUCCESS_LOCAL) {
			stopcapturing();
			uninitdevice();
			closedevice();
			LOGE("device resetted");
		}

	}

	if (ret != ERROR_LOCAL) {
		rgb = (int *) malloc(sizeof(int) * (IMG_WIDTH * IMG_HEIGHT));
		ybuf = (int *) malloc(sizeof(int) * (IMG_WIDTH * IMG_HEIGHT));
	}
	return ret;
}

jint Java_com_camera_simplewebcam_CameraPreview_prepareCameraWithBase(
		JNIEnv* env, jobject thiz, jint videoid, jint videobase) {
	LOGE("Java_com_camera_simplewebcam_CameraPreview_prepareCameraWithBase");
	int ret;

	camerabase = videobase;

	return Java_com_camera_simplewebcam_CameraPreview_prepareCamera(env, thiz,
			videoid);

}

void Java_com_camera_simplewebcam_CameraPreview_processCamera(JNIEnv* env,
		jobject thiz) {
	LOGE("Java_com_camera_simplewebcam_CameraPreview_processCamera");
	readframeonce();
}

void Java_com_camera_simplewebcam_CameraPreview_stopCamera(JNIEnv* env,
		jobject thiz) {
	LOGE("Java_com_camera_simplewebcam_CameraPreview_stopCamera");
	stopcapturing();

	uninitdevice();

	closedevice();

	if (rgb)
		free(rgb);
	if (ybuf)
		free(ybuf);

	fd = -1;

}

// [xuyulong] 获取MPEG数据并返回，由Java层来处理生成Bitmap
jint Java_com_camera_simplewebcam_CameraPreview_getCameraFrame(JNIEnv* env,
		jobject thiz, jbyteArray data) {
	struct v4l2_buffer buf;

	CLEAR(buf);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	//buf.memory = V4L2_MEMORY_MMAP;
	buf.memory = V4L2_MEMORY_USERPTR;

	if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
		switch (errno) {
		case EAGAIN:
			return 0;
		case EIO:
		default:
			return errnoexit("VIDIOC_DQBUF");
		}
	}

	assert(buf.index < n_buffers);
	LOGE("length = %d", buf.length);
	(*env)->SetByteArrayRegion(env, data, 0, buf.length,
			buffers[buf.index].start);

	if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
		return errnoexit("VIDIOC_QBUF");

	return 1;
}

