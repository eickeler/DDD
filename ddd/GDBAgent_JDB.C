// GDBAgent derived class to support JDB debugger
//
// Copyright (c) 2023-2025  Free Software Foundation, Inc.
// Written by Michael J. Eager <eager@gnu.org>
//
// This file is part of DDD.
// 
// DDD is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation; either
// version 3 of the License, or (at your option) any later version.
// 
// DDD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public
// License along with DDD -- see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
// 
// DDD is the data display debugger.
// For details, see the DDD World-Wide-Web page, 
// `http://www.gnu.org/software/ddd/',
// or send a mail to the DDD developers <ddd@gnu.org>.

#include "GDBAgent.h"
#include "GDBAgent_JDB.h"
#include "regexps.h"
#include "BreakPoint.h"
#include "Command.h"
#include "string-fun.h"
#include "disp-read.h"

char *GDBAgent_JDB_init_commands;
char *GDBAgent_JDB_settings;

GDBAgent_JDB::GDBAgent_JDB (XtAppContext app_context,
	      const string& gdb_call):
    GDBAgent (app_context, gdb_call, JDB)
{
    _title = "JDB";
    _has_frame_command = false;
    _has_display_command = false;
    _has_pwd_command = false;
    _has_make_command = false;
    _has_jump_command = false;
    _has_regs_command = false;
    _has_err_redirection = false;
    _has_examine_command = false;
    _has_attach_command = false;
    _has_unwatch_command = true;
    _program_language = LANGUAGE_JAVA;
}

// Return true iff ANSWER ends with primary prompt.
bool GDBAgent_JDB::ends_with_prompt (const string& ans)
{
    string answer = ans;
    strip_control(answer);

    // JDB prompts using "> " or "THREAD[DEPTH] ".  All these
    // prompts may also occur asynchronously.

#if RUNTIME_REGEX
    // Standard prompt: "THREAD[DEPTH] " or "> "
    static regex rxjdbprompt        
        ("([a-zA-Z][a-zA-Z0-9 ]*[a-zA-Z0-9][[][1-9][0-9]*[]]|>) ");
    // Same, but in reverse
    static regex rxjdbprompt_reverse
        (" (>|[]][0-9]*[1-9][[][a-zA-Z0-9][a-zA-Z0-9 ]*[a-zA-Z])");
    // Non-threaded prompt: "[DEPTH] " or "> "
    static regex rxjdbprompt_nothread
        ("(>|[[][1-9][0-9]*[]]) ");
#endif

    // Check for threaded prompt at the end of the last line
    string reverse_answer = reverse(answer);
    int match_len = rxjdbprompt_reverse.match(reverse_answer.chars(), 
    					      reverse_answer.length(), 0);
    if (match_len > 0)
    {
        last_prompt = reverse(reverse_answer.at(0, match_len));
        return true;
    }

    // Check for non-threaded prompt as the last line
    const int beginning_of_line = answer.index('\n', -1) + 1;
    const string possible_prompt = answer.from(beginning_of_line);
    if (possible_prompt.matches(rxjdbprompt_nothread))
    {
        last_prompt = possible_prompt;
        return true;
    }

    // Check for threaded prompt at the beginning of each line
    int last_nl = answer.length() - 1;
    while (last_nl >= 0)
    {
        last_nl = answer.index('\n', last_nl - answer.length());
        const int beginning_of_line = last_nl + 1;

        match_len = rxjdbprompt.match(answer.chars(), answer.length(), 
				      beginning_of_line);
        if (match_len > 0)
        {
	    int i = beginning_of_line + match_len;
	    while (i < int(answer.length()) && isspace(answer[i]))
		i++;
	    if (i < int(answer.length()) && answer[i] == '=')
	    {
	        // This is no prompt, but something like `dates[1] = 33'.
	    }
	    else
	    {
	        last_prompt = answer.at(beginning_of_line, match_len);
	        return true;
	    }
	}

	last_nl--;
    }

	return false;
}

bool GDBAgent_JDB::is_exception_answer(const string& answer) const
{
    // Any JDB backtrace contains these lines.
    return 
	 answer.contains("com.sun.tools.example.debug") ||
	 answer.contains("sun.tools.debug") ||
	 answer.contains("Internal exception:");
}

// Remove prompt
void GDBAgent_JDB::cut_off_prompt(string& answer) const
{
    // Check for prompt at the end of the last line
    if (answer.contains(last_prompt, -1))
    {
        answer = answer.before(int(answer.length()) - 
    			       int(last_prompt.length()));
    }
}

string GDBAgent_JDB::print_command(const char *expr, bool internal) const
{
    string cmd = "print";

    if (internal)
	cmd = "dump";

    if (strlen(expr) != 0) {
	cmd += ' ';
	cmd += expr;
    }

    return cmd;
}

string GDBAgent_JDB::info_locals_command() const { return "locals"; }

string GDBAgent_JDB::pwd_command() const { return ""; }

string GDBAgent_JDB::watch_command(const string& expr, WatchMode w) const
{
    if ((has_watch_command() & w) != w)
	return "";

    if ((w & WATCH_CHANGE) == WATCH_CHANGE)
	return "watch all " + expr;
    if ((w & WATCH_READ) == WATCH_READ)
	return "watch access " + expr;
    if ((w & WATCH_ACCESS) == WATCH_ACCESS)
	return "watch access " + expr;
    return "";
}

string GDBAgent_JDB::debug_command(const char *program, string args) const
{
    if (!args.empty() && !args.contains(' ', 0))
	args.prepend(' ');

    return string("load ") + program;
}

string GDBAgent_JDB::assign_command(const string& var, const string& expr) const
{
    string cmd = "set";

    if (has_debug_command())
	return "";		// JDB 1.1: not available

    cmd += " " + var + " ";

    switch (program_language())
    {
    case LANGUAGE_BASH:
    case LANGUAGE_C:
    case LANGUAGE_FORTRAN:
    case LANGUAGE_JAVA:
    case LANGUAGE_MAKE:
    case LANGUAGE_PERL:
    case LANGUAGE_PHP:
    case LANGUAGE_PYTHON:	// FIXME: vrbl names can conflict with commands
    case LANGUAGE_OTHER:
	cmd += "=";
	break;

    case LANGUAGE_ADA:
    case LANGUAGE_PASCAL:
    case LANGUAGE_CHILL:
	cmd += ":=";
	break;
    }

    return cmd + " " + expr;
}

// Parse breakpoint info response
void GDBAgent_JDB::parse_break_info (BreakPoint *bp, string &info) 
{
    // Actual parsing code is in BreakPoint
    bp->process_jdb (info);
}

// Command to restore breakpoint
// Return commands to restore this breakpoint, using the dummy number
// NR.  If AS_DUMMY is set, delete the breakpoint immediately in order
// to increase the breakpoint number.  If ADDR is set, use ADDR as
// (fake) address.  If COND is set, use COND as (fake) condition.
// Return true iff successful.
void GDBAgent_JDB::restore_breakpoint_command (std::ostream& os, 
                        BreakPoint *bp, string pos, string num,
                        string cond, bool as_dummy)
{
    /* Unused */ (void (bp)); (void (num)); (void (cond)); (void (as_dummy));
    os << "stop at " << pos << "\n";
}

// Create or clear a breakpoint at position A.  If SET, create a
// breakpoint; if not SET, delete it.  If TEMP, make the breakpoint
// temporary.  If COND is given, break only iff COND evals to true. W
// is the origin.
void GDBAgent_JDB::set_bp(const string& a, bool set, bool temp, const char *cond)
{
    (void) temp;

    CommandGroup cg;

    int new_bps = max_breakpoint_number_seen + 1;
    string address = a;

    if (address.contains('0', 0) && !address.contains(":"))
        address.prepend("*");        // Machine code address given

    if (!set)
    {
        // Clear bp
        gdb_command(clear_command(address));
    }
    else
    {
        if (is_file_pos(address))
            gdb_command("stop at " + address);
        else
            gdb_command("stop in " + address);
    }

    if (strlen(cond) != 0 && gdb->has_condition_command())
    {
        // Add condition
        gdb_command(gdb->condition_command(itostring(new_bps), cond));
    }
}
