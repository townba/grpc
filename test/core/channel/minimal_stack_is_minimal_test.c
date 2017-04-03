/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport_impl.h"
#include "test/core/util/test_config.h"

static int check_stack(const char *transport_name, grpc_channel_args *init_args,
                       grpc_channel_stack_type channel_stack_type, ...) {
  // create dummy channel stack
  grpc_channel_stack_builder *builder = grpc_channel_stack_builder_create();
  grpc_transport_vtable fake_transport_vtable = {.name = transport_name};
  grpc_transport fake_transport = {.vtable = &fake_transport_vtable};
  grpc_arg arg = {.type = GRPC_ARG_INTEGER,
                  .key = GRPC_ARG_MINIMAL_STACK,
                  .value.integer = 1};
  grpc_channel_stack_builder_set_target(builder, "foo.test.google.fr");
  grpc_channel_args *channel_args =
      grpc_channel_args_copy_and_add(init_args, &arg, 1);
  if (transport_name != NULL) {
    grpc_channel_stack_builder_set_transport(builder, &fake_transport);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_channel_stack_builder_set_channel_arguments(&exec_ctx, builder,
                                                     channel_args);
    GPR_ASSERT(
        grpc_channel_init_create_stack(&exec_ctx, builder, channel_stack_type));
    grpc_exec_ctx_finish(&exec_ctx);
  }

  // build up our expectation list
  gpr_strvec v;
  gpr_strvec_init(&v);
  va_list args;
  va_start(args, channel_stack_type);
  for (;;) {
    char *a = va_arg(args, char *);
    if (a == NULL) break;
    if (v.count != 0) gpr_strvec_add(&v, gpr_strdup(", "));
    gpr_strvec_add(&v, gpr_strdup(a));
  }
  va_end(args);
  char *expect = gpr_strvec_flatten(&v, NULL);
  gpr_strvec_destroy(&v);

  // build up our "got" list
  gpr_strvec_init(&v);
  grpc_channel_stack_builder_iterator *it =
      grpc_channel_stack_builder_create_iterator_at_first(builder);
  while (grpc_channel_stack_builder_move_next(it)) {
    const char *name = grpc_channel_stack_builder_iterator_filter_name(it);
    if (name == NULL) continue;
    if (v.count != 0) gpr_strvec_add(&v, gpr_strdup(", "));
    gpr_strvec_add(&v, gpr_strdup(name));
  }
  char *got = gpr_strvec_flatten(&v, NULL);
  gpr_strvec_destroy(&v);

  // figure out result, log if there's an error
  int result = 0;
  if (0 != strcmp(got, expect)) {
    gpr_log(GPR_ERROR,
            "FAILED transport=%s; stack_type=%d: expected '%s'; got '%s'",
            transport_name, channel_stack_type, expect, got);
    result = 1;
  }

  gpr_free(got);
  gpr_free(expect);

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_channel_stack_builder_destroy(&exec_ctx, builder);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  return result;
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  int errors = 0;
  errors += check_stack("unknown", NULL, GRPC_CLIENT_DIRECT_CHANNEL,
                        "connected", NULL);
  errors +=
      check_stack("unknown", NULL, GRPC_CLIENT_SUBCHANNEL, "connected", NULL);
  errors += check_stack("unknown", NULL, GRPC_SERVER_CHANNEL, "server",
                        "connected", NULL);
  errors += check_stack("chttp2", NULL, GRPC_CLIENT_DIRECT_CHANNEL,
                        "http-client", "connected", NULL);
  errors +=
      check_stack("chttp2", NULL, GRPC_CLIENT_SUBCHANNEL, "connected", NULL);
  errors += check_stack("chttp2", NULL, GRPC_SERVER_CHANNEL, "server",
                        "http-server", "connected", NULL);
  errors +=
      check_stack(NULL, NULL, GRPC_CLIENT_CHANNEL, "client-channel", NULL);
  GPR_ASSERT(errors == 0);
  grpc_shutdown();
  return 0;
}
