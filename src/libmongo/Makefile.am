AUTOMAKE_OPTIONS = foreign no-dependencies

if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror -DMONGO_HAVE_STDINT
endif

noinst_LTLIBRARIES = libmongo.la

libmongo_la_SOURCES = mongo.c bson.c md5.c numbers.c \
			mongo.h mongo_except.h platform_hacks.h md5.h bson.h

