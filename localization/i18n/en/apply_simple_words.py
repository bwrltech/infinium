#!/usr/bin/env python3
"""Write the Simple English words (simple_words.py) into Infinium_en.po as msgstr.

Run after editing simple_words.py, then compile:
    msgfmt --check-format -o resources/i18n/en/Infinium.mo localization/i18n/en/Infinium_en.po
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from simple_words import SIMPLE

PO = os.path.join(HERE, "Infinium_en.po")
FMT = re.compile(r"%(?:\d+\$)?[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[sdufgxc%]|%\d+%")

def escape(s):
    return s.replace('"', '\\"')

text = open(PO, encoding="utf-8").read()
done = set()

def replace(m):
    ctx, msgid = m.group("ctx"), m.group("id")
    key = (ctx + "\x04" + msgid) if ctx is not None else msgid
    new = SIMPLE.get(key)
    if new is None:
        # This catalog only holds the simple words: clear any entry dropped from the list
        return m.group(0) if msgid == "" else m.group("head") + 'msgstr ""'
    if sorted(FMT.findall(msgid)) != sorted(FMT.findall(new)) or msgid.count("\\n") != new.count("\\n"):
        sys.exit(f"placeholders or line breaks differ: {msgid!r} -> {new!r}")
    done.add(key)
    return m.group("head") + 'msgstr "' + escape(new).replace('\\\\"', '\\"') + '"'

entry = re.compile(r'(?P<head>(?:msgctxt "(?P<ctx>(?:[^"\\]|\\.)*)"\n)?msgid "(?P<id>(?:[^"\\]|\\.)*)"\n)msgstr "(?:[^"\\]|\\.)*"(?=\n)')
text = entry.sub(replace, text)
missing = set(SIMPLE) - done
for k in sorted(missing):
    print("not in Infinium_en.po:", repr(k))
open(PO, "w", encoding="utf-8").write(text)
print(f"{len(done)} simple words written")
