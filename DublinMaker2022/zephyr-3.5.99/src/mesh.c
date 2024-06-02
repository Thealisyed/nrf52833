#include <stdint.h>
#include <zephyr/sys/printk.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/sys/byteorder.h>
#include "dmb_message_type.h"
#include "mesh.h"

#define PUBLISHER_ADDR 0x000f
#define MOD_LF 0x0000
#define GROUP_ADDR 0xc00


#define OP_VENDOR_BUTTON BT_MESH_MODEL_OP_3(0x00, BT_COMP_ID_LF)
#define OP_DMB_MESSAGE BT_MESH_MODEL_OP_3(0xD0, BT_COMP_ID_LF)
#define OP_DMB_GAME_MESSAGE BT_MESH_MODEL_OP_3(0xD1,BT_COMP_ID_LF)
#define OP_DMB_GAME_MESSAGE_RESPONSE BT_MESH_MODEL_OP_3(0xD2,BT_COMP_ID_LF) 
#define OP_ONOFF_GET       BT_MESH_MODEL_OP_2(0x82, 0x01)
#define OP_ONOFF_SET       BT_MESH_MODEL_OP_2(0x82, 0x02)
#define OP_ONOFF_SET_UNACK BT_MESH_MODEL_OP_2(0x82, 0x03)
#define OP_ONOFF_STATUS    BT_MESH_MODEL_OP_2(0x82, 0x04)


static const uint16_t net_idx;
static const uint16_t app_idx;
static const uint32_t iv_index;
static uint8_t flags;
static uint16_t addr = PUBLISHER_ADDR;
volatile uint32_t DMBMessageReceived = 0;
volatile dmb_message DMBMailBox;
volatile uint16_t DMBMessageSender;
volatile uint32_t DMBGameMessageReceived = 0;
volatile dmb_message DMBGameMailBox;
volatile uint32_t DMBGameMessageResponseReceived = 0;
volatile dmb_message DMBGameResponseMailBox; 
void mesh_clearReplay(void);

static struct bt_mesh_cfg_cli cfg_cli = {
}; 

static void attention_on(struct bt_mesh_model *model)
{
	printk("attention_on()\n");
	
}

static void attention_off(struct bt_mesh_model *model)
{
	printk("attention_off()\n");
	
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

static const char *const onoff_str[] = { "off", "on" };

static struct {
	bool val;
	uint8_t tid;
	uint16_t src;
	uint32_t transition_time;
	struct k_work_delayable work;
} onoff;

static const uint32_t time_res[] = {
	100,
	MSEC_PER_SEC,
	10 * MSEC_PER_SEC,
	10 * 60 * MSEC_PER_SEC,
};

static inline int32_t model_time_decode(uint8_t val)
{
	uint8_t resolution = (val >> 6) & BIT_MASK(2);
	uint8_t steps = val & BIT_MASK(6);

	if (steps == 0x3f) {
		return SYS_FOREVER_MS;
	}

	return steps * time_res[resolution];
}

static inline uint8_t model_time_encode(int32_t ms)
{
	if (ms == SYS_FOREVER_MS) {
		return 0x3f;
	}

	for (int i = 0; i < ARRAY_SIZE(time_res); i++) {
		if (ms >= BIT_MASK(6) * time_res[i]) {
			continue;
		}

		uint8_t steps = DIV_ROUND_UP(ms, time_res[i]);

		return steps | (i << 6);
	}

	return 0x3f;
}

static int onoff_status_send(struct bt_mesh_model *model,
			     struct bt_mesh_msg_ctx *ctx)
{
	uint32_t remaining;

	BT_MESH_MODEL_BUF_DEFINE(buf, OP_ONOFF_STATUS, 3);
	bt_mesh_model_msg_init(&buf, OP_ONOFF_STATUS);

	remaining = k_ticks_to_ms_floor32(
			    k_work_delayable_remaining_get(&onoff.work)) +
		    onoff.transition_time;

	
	if (remaining) {
		net_buf_simple_add_u8(&buf, !onoff.val);
		net_buf_simple_add_u8(&buf, onoff.val);
		net_buf_simple_add_u8(&buf, model_time_encode(remaining));
	} else {
		net_buf_simple_add_u8(&buf, onoff.val);
	}

	return bt_mesh_model_send(model, ctx, &buf, NULL, NULL);
}

static void onoff_timeout(struct k_work *work)
{
	if (onoff.transition_time) {
		
		 
		board_led_set(true);

		k_work_reschedule(&onoff.work, K_MSEC(onoff.transition_time));
		onoff.transition_time = 0;
		return;
	}

	board_led_set(onoff.val);
}

static int gen_onoff_get(struct bt_mesh_model *model,
			 struct bt_mesh_msg_ctx *ctx,
			 struct net_buf_simple *buf)
{
	onoff_status_send(model, ctx);
	return 0;
}

static int gen_onoff_set_unack(struct bt_mesh_model *model,
			       struct bt_mesh_msg_ctx *ctx,
			       struct net_buf_simple *buf)
{
	uint8_t val = net_buf_simple_pull_u8(buf);
	uint8_t tid = net_buf_simple_pull_u8(buf);
	int32_t trans = 0;
	int32_t delay = 0;

	if (buf->len) {
		trans = model_time_decode(net_buf_simple_pull_u8(buf));
		delay = net_buf_simple_pull_u8(buf) * 5;
	}


	if (tid == onoff.tid && ctx->addr == onoff.src) {
		/* Duplicate */
		return 0;
	}

	if (val == onoff.val) {
		/* No change */
		return 0;
	}

	printk("set: %s delay: %d ms time: %d ms\n", onoff_str[val], delay,
	       trans);

	onoff.tid = tid;
	onoff.src = ctx->addr;
	onoff.val = val;
	onoff.transition_time = trans;

	/* Schedule the next action to happen on the delay, and keep
	 * transition time stored, so it can be applied in the timeout.
	 */
	k_work_reschedule(&onoff.work, K_MSEC(delay));

	return 0;
}


static int gen_onoff_set(struct bt_mesh_model *model,
			 struct bt_mesh_msg_ctx *ctx,
			 struct net_buf_simple *buf)
{
	(void)gen_onoff_set_unack(model, ctx, buf);
	onoff_status_send(model, ctx);

	return 0;
}

static const struct bt_mesh_model_op gen_onoff_srv_op[] = {
	{ OP_ONOFF_GET,       BT_MESH_LEN_EXACT(0), gen_onoff_get },
	{ OP_ONOFF_SET,       BT_MESH_LEN_MIN(2),   gen_onoff_set },
	{ OP_ONOFF_SET_UNACK, BT_MESH_LEN_MIN(2),   gen_onoff_set_unack },
	BT_MESH_MODEL_OP_END,
};

static int gen_onoff_status(struct bt_mesh_model *model,
			    struct bt_mesh_msg_ctx *ctx,
			    struct net_buf_simple *buf)
{
	uint8_t present = net_buf_simple_pull_u8(buf);

	if (buf->len) {
		uint8_t target = net_buf_simple_pull_u8(buf);
		int32_t remaining_time =
			model_time_decode(net_buf_simple_pull_u8(buf));

		printk("OnOff status: %s -> %s: (%d ms)\n", onoff_str[present],
		       onoff_str[target], remaining_time);
		return 0;
	}

	printk("OnOff status: %s\n", onoff_str[present]);

	return 0;
}

static const struct bt_mesh_model_op gen_onoff_cli_op[] = {
	{OP_ONOFF_STATUS, BT_MESH_LEN_MIN(1), gen_onoff_status},
	BT_MESH_MODEL_OP_END,
};

/*static struct bt_mesh_model root_models[] = {
	BT_MESH_MODEL_CFG_SRV,
	BT_MESH_MODEL_CFG_CLI(&cfg_cli),
	BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
}; */

static struct bt_mesh_model models[] = {
	BT_MESH_MODEL_CFG_SRV,
	BT_MESH_MODEL_CFG_CLI(&cfg_cli),
	BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_SRV, gen_onoff_srv_op, NULL,
		      NULL),
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_CLI, gen_onoff_cli_op, NULL,
		      NULL),
};

static int dmb_message_received(struct bt_mesh_model *model,
			       struct bt_mesh_msg_ctx *ctx,
			       struct net_buf_simple *buf)
{ 
    printk("DMB Message : ");
    for (int i=0;i<MAX_MESSAGE_LEN;i++)
    {    
        uint8_t data = net_buf_simple_pull_u8(buf);
		DMBMailBox.Message[i]=data;
        printk(" %X ",data);
    }
	printk("\n");
	printk("RSSI=%d\n",ctx->recv_rssi);
	DMBMessageReceived=1;
	DMBMessageSender=ctx->addr;
	mesh_clearReplay();

	return 0; 
}

static int dmb_game_message_received(struct bt_mesh_model *model,
			       struct bt_mesh_msg_ctx *ctx,
			       struct net_buf_simple *buf)
{ 
    printk("Game message received\n");
    for (int i=0;i<MAX_MESSAGE_LEN;i++)
    {    
        uint8_t data = net_buf_simple_pull_u8(buf);
        DMBGameMailBox.Message[i]=data;
        printk(" %X ",data);
    }
    DMBGameMessageReceived = 1;
	return 0; 
}
static int dmb_game_message_response_received(struct bt_mesh_model *model,
			       struct bt_mesh_msg_ctx *ctx,
			       struct net_buf_simple *buf)
{ 
    printk("Game response received\n");
    for (int i=0;i<MAX_MESSAGE_LEN;i++)
    {    
        uint8_t data = net_buf_simple_pull_u8(buf);
        DMBGameResponseMailBox.Message[i]=data;
        printk(" %X ",data);
    }
    DMBGameMessageResponseReceived = 1;
	return 0; 
}

			  
static const struct bt_mesh_model_op vnd_ops[] = {
	{ OP_DMB_MESSAGE, BT_MESH_LEN_EXACT(MAX_MESSAGE_LEN), dmb_message_received },
	{ OP_DMB_GAME_MESSAGE, BT_MESH_LEN_EXACT(MAX_MESSAGE_LEN), dmb_game_message_received },
    { OP_DMB_GAME_MESSAGE_RESPONSE, BT_MESH_LEN_EXACT(MAX_MESSAGE_LEN), dmb_game_message_response_received },
	BT_MESH_MODEL_OP_END
}; 

static struct bt_mesh_model vnd_models[] = {
	BT_MESH_MODEL_VND(BT_COMP_ID_LF, MOD_LF, vnd_ops, NULL, NULL),
}; 

        static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(0, models, vnd_models),
};

static const struct bt_mesh_comp comp = {
	.cid = BT_COMP_ID_LF,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};


static int output_number(bt_mesh_output_action_t action, uint32_t number)
{
	printk("OOB Number: %u\n", number);



	return 0;
}

static void prov_complete(uint16_t net_idx, uint16_t addr)
{
	//board_prov_complete();
}



static uint16_t the_target = GROUP_ADDR;

static void prov_reset(void)
{
	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
}

static uint8_t dev_uuid[16];

static const struct bt_mesh_prov prov = {
	.uuid = dev_uuid,
	.output_size = 4,
	.output_actions = BT_MESH_DISPLAY_NUMBER,
	.complete = prov_complete,
	.reset = prov_reset,
};


static int gen_onoff_send(bool val)
{
	struct bt_mesh_msg_ctx ctx = {
		.app_idx = models[3].keys[0], 
		.addr = BT_MESH_ADDR_ALL_NODES,
		.send_ttl = BT_MESH_TTL_DEFAULT,
	};
	static uint8_t tid;

	if (ctx.app_idx == BT_MESH_KEY_UNUSED) {
		printk("The Generic OnOff Client must be bound to a key before "
		       "sending.\n");
		return -ENOENT;
	}

	BT_MESH_MODEL_BUF_DEFINE(buf, OP_ONOFF_SET_UNACK, 2);
	bt_mesh_model_msg_init(&buf, OP_ONOFF_SET_UNACK);
	net_buf_simple_add_u8(&buf, val);
	net_buf_simple_add_u8(&buf, tid++);

	printk("Sending OnOff Set: %s\n", onoff_str[val]);

	return bt_mesh_model_send(&models[3], &ctx, &buf, NULL, NULL);
}

static void bt_ready(int err)
{
		
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	err = bt_mesh_init(&prov, &comp);
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return;
	}

	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

	printk("Mesh initialized\n");
	
	
}

void mesh_begin(uint16_t address)
{
	
	int err = -1;
	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	return;
}
}



void mesh_suspend()
{
	int err;
    err = bt_disable();
    if(err)
    {
        printk("Bluetooth disable failed (err %d)\n", err);
        return;
    }
    printk("Mesh Suspended\n");
}

void mesh_clearReplay()
{
	int err;
	bt_mesh_rpl_clear();
	

}
void mesh_resume()
{
	bt_mesh_resume();
}
void mesh_send_start(uint16_t duration, int err, void *cb_data)
{
        printk("send_start duration = %d, err = %d\n",duration,err);
}
void mesh_send_end(int err, void *cb_data)
{
       printk("send_end err=%d\n",err);
};
const struct bt_mesh_send_cb dmb_send_sb_s = { 
      .start = mesh_send_start,
       .end = mesh_send_end,
};

void sendDMBMessage(uint16_t the_target, dmb_message * dmbmsg)
{
	
		int err;
		NET_BUF_SIMPLE_DEFINE(msg, 3 + 4 + MAX_MESSAGE_LEN);
		struct bt_mesh_msg_ctx ctx = {
				.app_idx = app_idx,
				.addr = the_target,
				.send_ttl = BT_MESH_TTL_DEFAULT,
		};

		bt_mesh_model_msg_init(&msg, OP_DMB_MESSAGE);   
		for (int i=0;i<MAX_MESSAGE_LEN;i++)
		{
			net_buf_simple_add_u8(&msg,dmbmsg->Message[i]);
		}
		
		
		err = bt_mesh_model_send(&vnd_models[0], &ctx, &msg,&dmb_send_sb_s, NULL);
		if (err) {
				printk("Unable to send DMB message %d\n",err);
		}

		printk("DMB message sent with OpCode 0x%08x\n", OP_DMB_MESSAGE);
		mesh_clearReplay(); 

}

void sendDMBGameMessage(uint16_t the_target, dmb_message * dmbmsg)
{
      int err;
        NET_BUF_SIMPLE_DEFINE(msg, 3 + 4 + MAX_MESSAGE_LEN);
        struct bt_mesh_msg_ctx ctx = {
                .app_idx = app_idx,
                .addr = the_target,
                .send_ttl = BT_MESH_TTL_DEFAULT,
        };

        bt_mesh_model_msg_init(&msg, OP_DMB_GAME_MESSAGE);   
        for (int i=0;i<MAX_MESSAGE_LEN;i++)
        {
            net_buf_simple_add_u8(&msg,dmbmsg->Message[i]);
        }
        err = bt_mesh_model_send(&vnd_models[0], &ctx, &msg,&dmb_send_sb_s, NULL);
        if (err) {
                printk("Unable to send DMB game message %d\n",err); 
        }

        printk("DMB game message sent\n"); 
}

void sendDMBGameResponseMessage(uint16_t the_target, dmb_message * dmbmsg)
{
        int err;
        NET_BUF_SIMPLE_DEFINE(msg, 3 + 4 + MAX_MESSAGE_LEN);
        struct bt_mesh_msg_ctx ctx = {
                .app_idx = app_idx,
                .addr = the_target,
                .send_ttl = BT_MESH_TTL_DEFAULT,
        };

        bt_mesh_model_msg_init(&msg, OP_DMB_GAME_MESSAGE_RESPONSE);   
        printk("\nResponse data ");
        for (int i=0;i<MAX_MESSAGE_LEN;i++)
        {
            printk(" %X ",dmbmsg->Message[i]);
            net_buf_simple_add_u8(&msg,dmbmsg->Message[i]);
        }
        err = bt_mesh_model_send(&vnd_models[0], &ctx, &msg,&dmb_send_sb_s, NULL);
        if (err) {
                printk("Unable to send DMB game response message %d\n",err);
        }

        printk("DMB game response message sent\n"); 
}