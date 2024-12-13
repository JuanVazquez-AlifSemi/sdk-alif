/* Copyright (C) 2024 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 */
#include "inference_process.hpp"

#include <zephyr/shell/shell.h>
#include <inttypes.h>
#include <string>
#include <stdio.h>
#include <vector>
#include <zephyr/kernel.h>

#include "ethosu_worker.hpp"

static int cmd_start(const struct shell *shell, size_t argc, char **argv)
{
	Ethos_worker::start_eu();

	shell_fprintf(shell, SHELL_VT100_COLOR_DEFAULT, "Start Ethos-U55 inferencing\n");
	return 0;
}

static int cmd_stop(const struct shell *shell, size_t argc, char **argv)
{
	Ethos_worker::stop_eu();

	shell_fprintf(shell, SHELL_VT100_COLOR_DEFAULT, "Stop Ethos-U55 inferencing\n");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_cmds, SHELL_CMD_ARG(start, NULL, "start", cmd_start, 1, 10),
			       SHELL_CMD_ARG(stop, NULL, "stop", cmd_stop, 1, 10),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(ethosu, &sub_cmds, "Ethos-U55 commands", NULL);
