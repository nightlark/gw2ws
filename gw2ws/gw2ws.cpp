#include "gw2lib.h"
#include <sstream>
#include <thread>
#include <chrono>

extern "C" {
#include "mongoose.h"
}

// Need this line for GW2LIB to work...
GW2LIB::Font font;

struct MumbleContext {
	byte serverAddress[28]; // contains sockaddr_in or sockaddr_in6
	unsigned mapId;
	unsigned mapType;
	unsigned shardId;
	unsigned instance;
	unsigned buildId;
};

struct LinkedMem {
#ifdef _WIN32
	UINT32	uiVersion;
	DWORD	uiTick;
#else
	uint32_t uiVersion;
	uint32_t uiTick;
#endif
	float	fAvatarPosition[3];
	float	fAvatarFront[3];
	float	fAvatarTop[3];
	wchar_t	name[256];
	float	fCameraPosition[3];
	float	fCameraFront[3];
	float	fCameraTop[3];
	wchar_t	identity[256];
#ifdef _WIN32
	UINT32	context_len;
#else
	uint32_t context_len;
#endif
	MumbleContext gw2context;
	unsigned char context[208];
	wchar_t description[2048];
};

class ws_cli {
public:
	std::string name = "";
	bool authorized = false;
	bool locationUpdates = false;
	bool chatUpdates = false;
};

struct mg_mgr mgr;
struct mg_connection *ncserver;
static struct mg_serve_http_opts s_http_server_opts;
static int is_websocket(const struct mg_connection *nc);
static void broadcast(struct mg_connection *nc, const char *msg, size_t len);
static void ev_handler(struct mg_connection *nc, int ev, void *ev_data);
static void sendmsg(struct mg_connection *nc, const char *msg, size_t len);
static void broadcastLocation(struct mg_connection *nc, const char *msg, size_t len);


LinkedMem *lm = NULL;

static int is_websocket(const struct mg_connection *nc) {
	return nc->flags & MG_F_IS_WEBSOCKET;
}

static void broadcast(struct mg_connection *nc, const char *msg, size_t len) {
	struct mg_connection *c;
	char buf[1100];

	snprintf(buf, sizeof(buf), "%.*s", (int)len, msg);
	for (c = mg_next(nc->mgr, NULL); c != NULL; c = mg_next(nc->mgr, c))
	{
		mg_send_websocket_frame(c, WEBSOCKET_OP_TEXT, buf, strlen(buf));
	}
}

static void sendmsg(struct mg_connection *nc, const char *msg, size_t len) {
	char buf[1100];

	snprintf(buf, sizeof(buf), "%.*s", (int)len, msg);
	mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, buf, strlen(buf));
}

static void broadcastLocation(struct mg_connection *nc, const char *msg, size_t len) {
	struct mg_connection *c;
	char buf[1100];

	snprintf(buf, sizeof(buf), "%.*s", (int)len, msg);
	for (c = mg_next(nc->mgr, NULL); c != NULL; c = mg_next(nc->mgr, c))
	{
		// If the client is valid and has requested location updates, send them the update
		ws_cli *clientApp = (ws_cli*)c->user_data;
		if (clientApp != nullptr)
		{
			if (clientApp->locationUpdates)
			{
				mg_send_websocket_frame(c, WEBSOCKET_OP_TEXT, buf, strlen(buf));
			}
		}
	}
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
	struct http_message *hm = (struct http_message *) ev_data;
	struct websocket_message *wm = (struct websocket_message *) ev_data;
	ws_cli* clientApp;

	struct json_token *arr, *tok;
	char buf[1000];

	switch (ev)
	{
	case MG_EV_HTTP_REQUEST:
		/* Usual HTTP request - server static files */
		//mg_serve_http(nc, hm, s_http_server_opts);
		nc->flags |= MG_F_SEND_AND_CLOSE;
		break;
	case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
		clientApp = new ws_cli();
		nc->user_data = clientApp;
		sendmsg(nc, "{\"apiVersion\": \"0.0.1\", \"version\": 1}", 37);
		break;
	case MG_EV_WEBSOCKET_FRAME:
		clientApp = (ws_cli*)nc->user_data;

		// Tokenize json string, fill in tokens array
		arr = parse_json2((char *)wm->data, wm->size);
		if (arr == NULL)
		{
			sendmsg(nc, "{\"error\": \"Invalid JSON\"}", 25);
			break;
		}

		// Do not forget to free allocated tokens array
		if (clientApp->authorized)
		{
			// Access to the full API
			// Check if they are changing location update status
			tok = find_json_token(arr, "enableLocation");
			if (tok != NULL)
			{
				if (tok->type == JSON_TYPE_TRUE)
				{
					clientApp->locationUpdates = true;
				}
				else if (tok->type == JSON_TYPE_FALSE)
				{
					clientApp->locationUpdates = false;
				}
				else
				{
					sendmsg(nc, "{\"error\": \"Unrecognized value\"}", 31);
				}
			}

			// Check if they are changing chat update status
			clientApp->chatUpdates = false;
			tok = find_json_token(arr, "enableChat");
			if (tok != NULL)
			{
				if (tok->type == JSON_TYPE_TRUE)
				{
					clientApp->chatUpdates = true;
				}
				else if (tok->type == JSON_TYPE_FALSE)
				{
					clientApp->chatUpdates = false;
				}
				else
				{
					sendmsg(nc, "{\"error\": \"Unrecognized value\"}", 31);
				}
			}

			// Check for client requesting identity
			tok = find_json_token(arr, "requestIdentity");
			if (tok != NULL)
			{
				char ident[256];
				wcstombs(&ident[0], lm->identity, 256);

				if (sizeof(buf) >= json_emit(buf, sizeof(buf), "{s: S}",
					"identity",
					ident))
				{
					sendmsg(nc, buf, strlen(buf));
				}
			}

			// Check for client requesting context
			tok = find_json_token(arr, "requestContext");
			if (tok != NULL)
			{
				if (sizeof(buf) >= json_emit(buf, sizeof(buf), "{s: {s: i, s: i, s: i, s: i, s: i}}",
					"context",
					"mapId", lm->gw2context.mapId,
					"mapType", lm->gw2context.mapType,
					"shardId", lm->gw2context.shardId,
					"instance", lm->gw2context.instance,
					"buildId", lm->gw2context.buildId))
				{
					sendmsg(nc, buf, strlen(buf));
				}
				else
				{
					sendmsg(nc, "{\"error\": \"Context too big\"}", 28);
				}
			}
		}
		else
		{
			// Check to see if they are asking for authorization
			tok = find_json_token(arr, "appName");
			if (tok != NULL)
			{
				if (tok->type == JSON_TYPE_STRING && strlen(tok->ptr) > 0)
				{
					clientApp->name.append(tok->ptr);
					clientApp->authorized = true;
					sendmsg(nc, "{\"authorized\": true}", 20);
				}
			}
			else
			{
				sendmsg(nc, "{\"error\": \"Not authorized\"}", 27);
			}
		}

		// Free tokens array
		free(arr);
		break;
	case MG_EV_CLOSE:
		if (is_websocket(nc))
		{
			delete nc->user_data;
		}
		break;
	default:
		break;
	}
}

void GW2LIB::gw2lib_main()
{
	DWORD lastUiTick = 0;

	HANDLE hMapObject = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"MumbleLink");
	if (hMapObject == NULL)
	{
		hMapObject = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_EXECUTE_READWRITE, 0, 5460, L"MumbleLink");
		if (hMapObject == NULL)
		{
			return;
		}
	}

	lm = (LinkedMem *)MapViewOfFile(hMapObject, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinkedMem));
	if (lm == NULL) {
		hMapObject = NULL;
		return;
	}

	mg_mgr_init(&mgr, NULL);

	ncserver = mg_bind(&mgr, "1234", ev_handler);
	//s_http_server_opts.document_root = ".";
	mg_set_protocol_http_websocket(ncserver);

	while (GetAsyncKeyState(VK_HOME) >= 0)
	{
		mg_mgr_poll(&mgr, 15);
		if (lastUiTick < lm->uiTick)
		{
			// Broadcast an update to anyone that wanted location updates
			lastUiTick = lm->uiTick;
			char buf[1000];
			if (sizeof(buf) >= json_emit(buf, sizeof(buf), "{s: {s: [f, f, f], s: [f, f, f], s: [f, f, f], s: [f, f, f], s: [f, f, f], s: [f, f, f]}}",
				"movementUpdate",
				"fAvatarPosition", lm->fAvatarPosition[0], lm->fAvatarPosition[2], lm->fAvatarPosition[1],
				"fAvatarFront", lm->fAvatarFront[0], lm->fAvatarFront[2], lm->fAvatarFront[1],
				"fAvatarTop", lm->fAvatarTop[0], lm->fAvatarTop[2], lm->fAvatarTop[1],
				"fCameraPosition", lm->fCameraPosition[0], lm->fCameraPosition[2], lm->fCameraPosition[1],
				"fCameraFront", lm->fCameraFront[0], lm->fCameraFront[2], lm->fCameraFront[1],
				"fCameraTop", lm->fCameraTop[0], lm->fCameraTop[2], lm->fCameraTop[1]))
			{
				broadcastLocation(ncserver, buf, strlen(buf));
			}
			else
			{
				broadcastLocation(ncserver, "{\"error\": Location too big}", 27);
			}
		}
	}
	CloseHandle(hMapObject);

	// Let all our clients know we are shutting down
	broadcast(ncserver, "{\"status\": \"shutdown\"}", 22);

	mg_mgr_free(&mgr);
}