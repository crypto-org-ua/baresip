/**
 * @file vpn.c ZRTP: VPN mixing driver
 *
 * Copyright (C) Ignat Korchagin
 */
#include <re.h>
#include <baresip.h>
#include <string.h>
#include <pthread.h>

#include "vnic.h"

/*struct menc_sess {
	void *session;
};*/

struct menc_media
{
	struct udp_helper *uh;
	void *rtpsock;
	struct sa raddr;
	pthread_t send_thread;
	int run;
	vnic_handle vnic;
};

struct vpnhdr
{
	uint32_t length;
	uint32_t magic;
};

/* Following traditions of STUN and ZRTP we will make a custom non-RTP header for our packets */
/*=============================================================================================
          0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |0 0|   Not used (set to zero)  |     Message Length in bytes   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                      Magic Cookie (MVPN)                      |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 =============================================================================================*/

static const uint8_t vpn_magic[] = {'M', 'V', 'P', 'N'};
static char tundev[64] = { 0 };

/*static void session_destructor(void *arg)
{
	struct menc_sess *st = arg;
}*/


static void media_destructor(void *arg)
{
	struct menc_media *st = arg;

	if (st->run)
	{
		st->run = 0;
		(void)pthread_join(st->send_thread, NULL);
	}

	if (0 < st->vnic)
		vnic_close(st->vnic);

	mem_deref(st->uh);
	mem_deref(st->rtpsock);
}

/*static bool udp_helper_send(int *err, struct sa *dst,
			    struct mbuf *mb, void *arg)
{
	struct menc_media *st = arg;
	unsigned int length;

	return false;
}*/


static bool udp_helper_recv(struct sa *src, struct mbuf *mb, void *arg)
{
	struct menc_media *st = arg;
	struct vpnhdr *hdr;
	uint32_t length;

	(void)src;

	if ((!mb) || (!arg))
		return false;

	if (sizeof(struct vpnhdr) > mbuf_get_left(mb))
		return false;

	hdr = (struct vpnhdr *)mbuf_buf(mb);
	if (memcmp(vpn_magic, &(hdr->magic), sizeof(vpn_magic)))
		return false;

	length = ntohl(hdr->length);
	if (0xffff0000 & length)
		return false;

	length &= 0x0000ffff;
	if (sizeof(struct vpnhdr) + length > mbuf_get_left(mb))
		return false;

	(void)vnic_write(st->vnic, hdr + 1, length);
	return true;
}


/*static int session_alloc(struct menc_sess **sessp, struct sdp_session *sdp,
			 bool offerer, menc_error_h *errorh, void *arg)
{
	struct menc_sess *st;
	int err = 0;
	(void)offerer;
	(void)errorh;
	(void)arg;

	if (!sessp || !sdp)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), session_destructor);
	if (!st)
		return ENOMEM;

 out:
	if (err)
		mem_deref(st);
	else
		*sessp = st;

	return err;
}*/

static void* send_routine(void *arg)
{
	struct menc_media *st = arg;
	unsigned char buffer[2048];
	ssize_t bytes_read = 0;

	while (st->run)
	{
		if (0 < vnic_wait_for_data(st->vnic))
		{
			bytes_read = vnic_read(st->vnic, buffer + sizeof(struct vpnhdr), sizeof(buffer) - sizeof(struct vpnhdr));
			if (0 < bytes_read)
			{
				struct vpnhdr *hdr = (struct vpnhdr *)buffer;
				struct mbuf *mb;

				memcpy(&(hdr->magic), vpn_magic, sizeof(vpn_magic));
				hdr->length = htonl(0x0000ffff & bytes_read);

				if (!sa_isset(&st->raddr, SA_ALL))
					continue;

				mb = mbuf_alloc(bytes_read + sizeof(struct vpnhdr));
				if (!mb)
					continue;

				(void)mbuf_write_mem(mb, (void *)buffer, bytes_read + sizeof(struct vpnhdr));
				mb->pos = 0;

				(void)udp_send(st->rtpsock, &st->raddr, mb);

				mem_deref(mb);
			}
		}
	}

	return NULL;
}

static int media_alloc(struct menc_media **stp, struct menc_sess *sess,
		       struct rtp_sock *rtp,
		       int proto, void *rtpsock, void *rtcpsock,
		       struct sdp_media *sdpm)
{
	struct menc_media *st;
	int err = 0;
	(void)sess;
	(void)rtp;
	(void)rtcpsock;
	(void)sdpm;

	if (!stp || proto != IPPROTO_UDP)
		return EINVAL;

	st = *stp;
	if (st)
		goto out;

	st = mem_zalloc(sizeof(*st), media_destructor);
	if (!st)
		return ENOMEM;

	st->rtpsock = mem_ref(rtpsock);

	err = udp_register_helper(&(st->uh), rtpsock, 0,
				  NULL, udp_helper_recv, st);
	if (err)
		goto out;

	st->vnic = vnic_open(tundev);
	if (-1 == st->vnic)
	{
		err = -1;
		goto out;
	}

	st->run = 1;
	err = pthread_create(&(st->send_thread), NULL, send_routine, st);
	if (err)
	{
		st->run = 0;
		goto out;
	}

 out:
	if (err)
	{
		mem_deref(st);
		return err;
	}
	else
	{
		if (sa_isset(sdp_media_raddr(sdpm), SA_ALL))
			st->raddr = *sdp_media_raddr(sdpm);

		*stp = st;
	}

	return err;
}

static struct menc menc_vpn = {
	LE_INIT, "vpn", "RTP/AVP", NULL, media_alloc
};

static void config_parse(struct conf *conf)
{
	if (conf_get_str(conf, "vpn_device", tundev, sizeof(tundev)))
	{
		/* Reading configuration failed, setting to default value */
		strcpy(tundev, "tun0");
	}
}

static int module_init(void)
{
	config_parse(conf_cur());
	menc_register(&menc_vpn);
	return 0;
}

static int module_close(void)
{
	menc_unregister(&menc_vpn);
	return 0;
}

EXPORT_SYM const struct mod_export DECL_EXPORTS(vpn) = {
	"vpn",
	"menc",
	module_init,
	module_close
};
