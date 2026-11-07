/*
 * pc_options_host.h - child window for the PC options page.
 *
 * The page is a borderless child of the D3D11 window, placed inside the
 * 4:3 picture. It is not drawn by the swap chain, and it is not the Xbox
 * Options screen. docs/superpowers/specs/2026-11-06-pc-options-design.md
 */
#ifndef PC_OPTIONS_HOST_H
#define PC_OPTIONS_HOST_H

#include "pc_options.h"

#ifdef __cplusplus
extern "C" {
#endif

void pc_options_host_start(void);

/* Any thread. Filters this poll and queues a repaint or a resize. */
struct pc_pad_result pc_options_host_on_port(int port, unsigned buttons);

#ifdef __cplusplus
}
#endif

#endif /* PC_OPTIONS_HOST_H */
