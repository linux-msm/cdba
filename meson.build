# SPDX-License-Identifier: GPL-2.0

project('cdba',
	'c',
	license : [ 'BSD-3-Clause'],
	meson_version : '>= 0.47.0', # for feature user options
	default_options: [
            'warning_level=2',	# sets -Wextra
            'buildtype=release',
        ])

# Set advanced compiler flags
compiler = meson.get_compiler('c')

compiler_cflags = ['-Wno-unused-parameter',
		   '-Wno-unused-result',
		   '-Wno-missing-field-initializers',
		   '-Wno-sign-compare',
		   '-Wundef',
		   '-Wnull-dereference',
		   '-Wdouble-promotion',
		   '-Wshadow',
		   '-Wpointer-arith',
		   '-Wwrite-strings',
		   '-Wstrict-overflow=4']

# TODO add clang specific options
if compiler.get_id() == 'gcc'
	compiler_cflags += ['-Wformat-signedness',
			    '-Wduplicated-cond',
			    '-Wduplicated-branches',
			    '-Wvla-larger-than=1',
			    '-Walloc-zero',
			    '-Wunsafe-loop-optimizations',
			    '-Wcast-align',
			    '-Wlogical-op',
			    '-Wjump-misses-init']
endif

add_global_arguments(compiler.get_supported_arguments(compiler_cflags),
		     language: 'c')

client_srcs = ['cdba.c',
	       'circ_buf.c']
executable('cdba',
	   client_srcs,
	   install : true)

server_opt = get_option('server')

ftdi_dep = dependency('libftdi1', required: false)
if not ftdi_dep.found()
	ftdi_dep = dependency('libftdi', required: server_opt)
endif

gpiod_dep = dependency('libgpiod', required: server_opt)
cdbalib_deps = [dependency('libudev', required: server_opt),
	       dependency('yaml-0.1', required: server_opt),
	       gpiod_dep,
	       ftdi_dep]

# E.g. Debian reuires -lutil for forkpty
if not compiler.has_function('forkpty')
  util_dep = compiler.find_library('util')
  cdbalib_deps += util_dep
endif

drivers_srcs = ['drivers/alpaca.c',
	        'drivers/cdb_assist.c',
		'drivers/conmux.c',
		'drivers/external.c',
		'drivers/ftdi-gpio.c',
		'drivers/laurent.c',
		'drivers/local-gpio.c',
		'drivers/qcomlt_dbg.c',
		]

if gpiod_dep.version().version_compare('>=2.0')
	drivers_srcs += ['drivers/local-gpio-v2.c']
else
	drivers_srcs += ['drivers/local-gpio-v1.c']
endif

cdbalib_srcs = ['circ_buf.c',
	       'device.c',
	       'device_parser.c',
	       'fastboot.c',
	       'console.c',
	       'ppps.c',
               'status.c',
               'status-cmd.c',
               'watch.c',
               'tty.c']

server_srcs = ['cdba-server.c']

build_server = true
foreach d: cdbalib_deps
  if not d.found()
    build_server = false
  endif
endforeach

if build_server
	camera_opt = get_option('camera')
	jpeg_dep = dependency('libjpeg', required: camera_opt)

	build_camera = true
	server_cflags = []
	if not jpeg_dep.found()
		build_camera = false
		server_cflags += '-DCDBA_CAMERA_DISABLED'
	else
		cdbalib_srcs += ['camera.c']
		cdbalib_deps += jpeg_dep
	endif

	libcdba = static_library('cdba',
				cdbalib_srcs + drivers_srcs,
				dependencies : cdbalib_deps,
				)

	executable('cdba-server',
		  server_srcs,
		  link_with : libcdba,
		  install : true,
		  c_args : server_cflags)
        executable('cdba-power',
                  ['cdba-power.c'],
		  link_with : libcdba,
		  install : true)
elif not server_opt.disabled()
	message('Skipping CDBA server build')
endif
