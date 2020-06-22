#! /usr/bin/env python3
#
# ccheck.py  Code Checker
#
# Copyright (C) 2005 - 2012 Alfred E. Heggestad
# Copyright (C) 2010 - 2012 Creytiv.com
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License version 2 as
#    published by the Free Software Foundation.
#
# Contributors:
#
#    Haavard Wik Thorkildssen <havard.thorkildssen@telio.ch>
#    Mal Minhas <mal@malm.co.uk>
#    Sebastian Reimers
#
#
# TODO:
# - optimize regex functions
# - count max y lines
#

import sys, os, re, fnmatch, getopt

PROGRAM = 'ccheck'
VERSION = '0.2.0'
AUTHOR  = 'Alfred E. Heggestad'


###
### Class definition
###

class ccheck:

    def __init__(self):
        self.errors = 0
        self.cur_filename = ''
        self.cur_lineno = 0
        self.empty_lines_count = 0
        self.cc_count = 0
        self.files = {}
        self.extensions = ['c', 'cpp', 'h', 'mk', 'm4', 'py', 'm', 's', 'java',
                           'php']

        self.operators = ["do", "if", "for", "while", "switch"]
        self.re_tab  = re.compile('(\w+\W*)\(')
        self.re_else = re.compile('\s*\}\s*else')
        self.re_inc  = re.compile('(^\s+\w+[+-]{2};)')
        self.re_hex  = re.compile('0x([0-9A-Fa-f]+)')

        # empty dict
        for e in self.extensions:
            self.files[e] = []

        # todo: global config
        self.common_checks = [self.check_whitespace, self.check_termination,
                              self.check_hex_lowercase, self.check_pre_incr,
                              self.check_file_unix]
        self.funcmap = {
            'c':    [self.check_brackets, self.check_c_preprocessor,
                     self.check_indent_tab],
            'h':    [self.check_brackets, self.check_indent_tab],
            'cpp':  [self.check_brackets, self.check_indent_tab],
            'mk':   [self.check_indent_tab],
            'm4':   [self.check_brackets, self.check_c_comments,
                     self.check_indent_tab],
            'py':   [self.check_brackets, self.check_indent_space],
            'm':    [self.check_brackets, self.check_c_preprocessor,
                     self.check_indent_tab],
            's':    [self.check_indent_tab, self.check_c_preprocessor],
            'java': [self.check_brackets, self.check_indent_tab],
            'php':  [self.check_brackets, self.check_indent_tab],
            }
        self.extmap = {
            'c':    ['*.c'],
            'h':    ['*.h'],
            'cpp':  ['*.cpp', '*.cc'],
            'mk':   ['*Makefile', '*.mk'],
            'm4':   ['*.m4'],
            'py':   ['*.py'],
            'm':    ['*.m'],
            's':    ['*.s', '*.S'],
            'java': ['*.java'],
            'php':  ['*.php'],
            }
        self.maxsize = {
            'c':    (79, 3000),
            'h':    (79, 1000),
            'cpp':  (79, 3000),
            'mk':   (79, 1000),
            'm4':   (79, 3000),
            'py':   (79, 3000),
            'm':    (79, 3000),
            's':    (79, 3000),
            'java': (179, 3000),
            'php':  (179, 3000),
            }


    def __del__(self):
        pass


    #
    # print an error message and increase error count
    #
    def error(self, msg):
        print("%s:%d: %s" % \
              (self.cur_filename, self.cur_lineno, msg), file=sys.stderr)
        self.errors += 1


    #
    # print statistics
    #
    def print_stats(self):
        print("Statistics:")
        print("~~~~~~~~~~~")
        print("Number of files processed:   ", end=' ')
        for e in self.extensions:
            print(" %s: %d" % (e, len(self.files[e])), end=' ')
        print("")
        print("Number of lines with errors:   %d" % self.errors)
        print("")


    #
    # check for strange white space
    #
    def check_whitespace(self, line, len):

        if len > 0:
            # general trailing whitespace check
            if line[-1] == ' ':
                self.error("has trailing space(s)")

            if line[-1] == '\t':
                self.error("has trailing tab(s)")

        # check for empty lines count
        if self.cur_lineno == 1:
            self.empty_lines_count = 0
        if len == 0:
            self.empty_lines_count += 1
        else:
            self.empty_lines_count = 0
        if self.empty_lines_count > 2:
            self.error("should have maximum two empty lines (%d)" % \
                       self.empty_lines_count)
            self.empty_lines_count = 0


    #
    # check for strange white space
    #
    def check_indent_tab(self, line, len):

        # make sure TAB is used for indentation
        for n in range(4, 17, 4):
            if len > n and line[0:n] == ' '*n and line[n] != ' ':
                self.error("starts with %d spaces, use tab instead" % n)


    def check_indent_space(self, line, len):

        if len > 1 and line[0] == '\t':
            self.error("starts with TAB, use 4 spaces instead")


    #
    # check for end of line termination issues
    #
    def check_termination(self, line, len):

        if len < 2:
            return

        if line[-2:] == ';;':
            self.error("has double semicolon")

        if line[-2:] == ' ;' and re.search('[\S]+[ \t]+;$', line):
            self.error("has spaces before terminator")


    #
    # check for C++ comments
    #
    def check_c_preprocessor(self, line, len):

        index = line.find('//')
        if index != -1 and line[index-1] != ':':
            if not re.search('["]+.*//.*["]+', line):
                self.error("C++ comment, use C comments /* ... */ instead")


    #
    # check that C comments are not used
    #
    def check_c_comments(self, line, len):

        if self.cur_lineno == 1:
            self.cc_count = 0

        cc = False

        if line.find('/*') != -1:
            self.cc_count += 1

        if line.find('*/') != -1:
            if self.cc_count > 0:
                cc = True
            self.cc_count = 0

        if cc:
            self.error("C comment, use Perl-style comments # ... instead");


    #
    # check max line length and number of lines
    #
    def check_xy_max(self, line, line_len, max_x):

        # expand TAB to 8 spaces
        l = len(line.expandtabs())

        if l > max_x:
            self.error("line is too wide (" + str(l) + " - max " \
                       + str(max_x) + ")");

        #    TODO:
        #    if ($line > $max_y) {
        #      self.error("is too big ($lines lines - max $max_y)\n");


    #
    # check that hexadecimal numbers are lowercase
    #
    def check_hex_lowercase(self, line, len):

        m = self.re_hex.search(line)
        if m:
            a = m.group(1)
            if re.search('[A-F]+', a):
                self.error("0x%s should be lowercase" % a)


    #
    # check for correct brackets usage in C/C++
    #
    # TODO: this is too slow, optimize
    #
    def check_brackets(self, line, len):

        m = self.re_tab.search(line)
        if m:
            keyword = m.group(1)

            if keyword.strip() in self.operators:
                if not re.search('[ ]{1}', keyword):
                    self.error("no single space after operator '%s()'" \
                               % keyword)

        # check that else statements do not have preceeding
        # end-bracket on the same line
        if self.re_else.search(line):
            self.error("else: ending if bracket should be on previous line")


    #
    # check that file is in Unix format
    #
    def check_file_unix(self, line, len):

        if len < 1:
            return

        if line[-1] == '\r':
            self.error("not in Unix format");


    #
    # check for post-increment/decrement
    #
    def check_pre_incr(self, line, len):

        m = self.re_inc.search(line)
        if m:
            op = m.group(1)
            if op.find('++') != -1:
                self.error("Use pre-increment: %s" % op);
            else:
                self.error("Use pre-decrement: %s" % op);


    def process_line(self, line, funcs, ext):

        line = line.rstrip('\n')
        line_len = len(line)

        for func in self.common_checks:
            func(line, line_len)

        for func in funcs:
            func(line, line_len)

        if ext in self.maxsize:
            (x, y) = self.maxsize[ext];
            self.check_xy_max(line, line_len, x)


    def parse_file(self, filename, ext):

        funcs = self.funcmap[ext]

        f = open(filename)

        self.cur_filename = filename

        while 1:
            lines = f.readlines(100000)
            if not lines:
                break
            self.cur_lineno = 0
            for line in lines:
                self.cur_lineno += 1
                self.process_line(line, funcs, ext)


    def parse_any_file(self, f):
        for e in self.extensions:
            em = self.extmap[e]
            for m in em:
                if fnmatch.fnmatch(f, m):
                    self.files[e].append(f)
                    self.parse_file(f, e)
                    return
        print("unknown extension: " + f)


    def rec_quasiglob(self, top, patterns, exclude):
        for root, dirs, files in os.walk(top, topdown=False):
            for f in files:
                for pattern in patterns:
                    if fnmatch.fnmatch(f, pattern):
                        path = os.path.join(root, f)
                        parse = True
                        for excl in exclude:
                            if path.find(excl) >= 0:
                                parse = False

                        if parse:
                            self.parse_any_file(path)


    def build_file_list(self, top, exclude):
        for e in self.extensions:
            em = self.extmap[e]
            self.rec_quasiglob(top, em, exclude)


###
### END OF CLASS
###


def usage():
    print("%s version %s" % (PROGRAM, VERSION))
    print("")
    print("Usage:")
    print("")
    print("  %s [options] [file]... [dir]..." % PROGRAM)
    print("")
    print("options:")
    print("")
    print("  -h --help     Display help")
    print("  -V --version  Show version info")
    print("  -q --quiet    Print warnings only")
    print("  -e --exclude  Exclude pattern(s)")


#
# main program
#


def main():
    quiet = False
    exclude = []
    try:
        opts, args = getopt.getopt(sys.argv[1:], \
                                   'hVqe:',
                                   ['help', 'version', 'quiet', 'exclude='])
    except getopt.GetoptError as err:
        print(str(err))
        usage()
        sys.exit(2)
    for o, a in opts:
        if o in ('-h', '--help'):
            usage()
            sys.exit()
        elif o in ('-V', '--version'):
            print("%s version %s, written by %s" % (PROGRAM, VERSION, AUTHOR))
            sys.exit()
        elif o in ('-q', '--quiet'):
            quiet = True
        elif o in ('-e', '--exclude'):
            exclude.append(a)
        else:
            assert False, "unhandled option"

    cc = ccheck()

    if len(args) >= 1:
        for f in args[0:]:
            if os.path.isdir(f):
                cc.build_file_list(f, exclude)
            elif os.path.isfile(f):
                cc.parse_any_file(f)
            else:
                print("unknown file type: " + f)
    else:
        # scan all files recursively
        cc.build_file_list('.', exclude)

    # done - print stats
    if not quiet:
        cc.print_stats()

    sys.exit(cc.errors != 0)


if __name__ == "__main__":
    main()
