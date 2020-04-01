/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright (C) 2019 ScyllaDB
 */

#pragma once

// Utilities for API versioning.
//
// API levels are added in pairs. The odd number is for the to-be-replaced
// symbols, while the even number is for the new version of those symbols.

// For IDEs that don't see SEASTAR_API_LEVEL, generate a nice default
#ifndef SEASTAR_API_LEVEL
#define SEASTAR_API_LEVEL 2
#endif

#if SEASTAR_API_LEVEL >= 2

#define SEASTAR_INCLUDE_API_V2 inline
#define SEASTAR_INCLUDE_API_V1

#else

#define SEASTAR_INCLUDE_API_V2
#define SEASTAR_INCLUDE_API_V1 inline

#endif

#if SEASTAR_API_LEVEL >= 4

#define SEASTAR_INCLUDE_API_V3
#define SEASTAR_INCLUDE_API_V4 inline

#elif SEASTAR_API_LEVEL <= 3

#define SEASTAR_INCLUDE_API_V3 inline
#define SEASTAR_INCLUDE_API_V4

#endif
