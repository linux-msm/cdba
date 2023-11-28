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
server_deps = [dependency('libudev', required: server_opt),
	       dependency('yaml-0.1', required: server_opt),
	       gpiod_dep,
	       ftdi_dep]

# E.g. Debian reuires -lutil for forkpty
if not compiler.has_function('forkpty')
  util_dep = compiler.find_library('util')
  server_deps += util_dep
endif

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
	       'ppps.c',
               'status.c',
               'status-cmd.c']

if gpiod_dep.version().version_compare('>=2.0')
	server_srcs += ['local-gpio-v2.c']
else
	server_srcs += ['local-gpio-v1.c']
endif

build_server = true
foreach d: server_deps
  if not d.found()
    build_server = false
  endif
endforeach

if build_server
	executable('cdba-server',
		  server_srcs,
		  dependencies : server_deps,
		  install : true)
elif not server_opt.disabled()
	message('Skipping CDBA server build')
endif
