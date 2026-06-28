// =============================================================================
//  webserver.h — WiFi (STA+AP) · mDNS · AsyncWebServer · WebSocket
//  Input web → cmdSend() (antrian). Output → snapshot read-only.
// =============================================================================
#ifndef WEBSERVER_H
#define WEBSERVER_H

void webInit();        // dipanggil di setup() (boleh blocking saat connect)
void webBroadcast();   // dipanggil dari wsTask (cleanup + textAll)

#endif // WEBSERVER_H
