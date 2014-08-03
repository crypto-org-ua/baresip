#
# module.mk
#
# Copyright (C) Ignat Korchagin
#

MOD		:= vpn
$(MOD)_SRCS	+= vpn.c vnic.c

include mk/mod.mk
