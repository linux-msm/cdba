# SPDX-License-Identifier: GPL-2.0

project('cdba',
	'c',
	license : [ 'BSD-3-Clause'],
	meson_version : '>= 0.43.0', # for compiler.get_supported_arguments()
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
	compiler_cflags += ['-Werror',	# Only set it on GCC
			    '-Wformat-signedness',
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

ftdi_dep = dependency('libftdi1', required: false)
if not ftdi_dep.found()
	ftdi_dep = dependency('libftdi')
endif

gpiod_dep = dependency('libgpiod')
server_deps = [dependency('libudev'),
	       dependency('yaml-0.1'),
	       gpiod_dep,
	       ftdi_dep]
server_srcs = ['cdba-server.c',
	       'cdb_assist.c',
	       'circ_buf.c',
	       'conmux.c',
	       'device.c',
	       'device_parser.c',
               'external.c',
	       'fastboot.c',
	       'alpaca.c',
	       'ftdi-gpio.c',
	       'local-gpio.c',
	       'console.c',
	       'qcomlt_dbg.c',
	       'ppps.c']

if gpiod_dep.version().version_compare('>=2.0')
	server_srcs += ['local-gpio-v2.c']
else
	server_srcs += ['local-gpio-v1.c']
endif

executable('cdba-server',
	   server_srcs,
	   dependencies : server_deps,
	   install : true)
