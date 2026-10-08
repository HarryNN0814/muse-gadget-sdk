/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The screen's own text in CONFIG_MUSE_UI_LANGUAGE. The code says it in
 * English, and the serial console and the tools that read it keep that; only
 * what's drawn is translated, by these lookups (tables in muse_lang.c). Each
 * returns its translation of `en`, a format string's included, or `en` itself
 * when it has none.
 */

/* The state label's short, upper-case names ("READY") and the power label's. */
const char *muse_lang_status(const char *en);

/* Captions ("CAN'T REACH MUSE"). */
const char *muse_lang_message(const char *en);

#ifdef __cplusplus
}
#endif
