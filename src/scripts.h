#pragma once

static const char *uncompile_lib_script = R"ADFAAF(
import blue
import zipfile
import zlib
import os
import marshal
import uncompyle2

# Run this code within EVE works on NEW eve builds
with zipfile.ZipFile(input_path, "r") as zf:
    print input_path + " opened!"
    for filename in zf.namelist():
        filename = os.path.normpath(filename.replace('\\', '/'))
        if (os.path.isabs(filename) or filename == '..' or
            filename.startswith('../') or ':' in filename.split('/')[0]):
            raise ValueError("Unsafe library entry: " + repr(filename))
        if "appConst.pyj" in filename:
            continue
        if filename[-4:] == ".pyj":
            print filename
            data = blue.UnjumbleString(zf.read(filename), True)
            code = marshal.loads(data[8:])
            head, tail = os.path.split(filename)
            newfile = os.path.join(output_path, head)
            if not os.path.isdir(newfile):
                os.makedirs(newfile)

            out_file = os.path.join(newfile, tail[:-4] + ".pyo")
            out_file_py = os.path.join(newfile, tail[:-4] + ".py")

            f = open(out_file_py, "wb")
            print " >> decompiling " + out_file + " -> " + out_file_py
            try:
                try:
                    uncompyle2.uncompyle('2.7', code, f)
                except Exception as original_error:
                    print " >> uncompyle2 failed; retrying with uncompyle6: " + str(original_error)
                    f.seek(0)
                    f.truncate()
                    from uncompyle6.main import decompile
                    decompile(code, out=f)
            except Exception as e:
                f.close()
                os.remove(out_file_py)
                raise RuntimeError("Unable to decompile %s: %s" % (filename, e))
            f.close()
)ADFAAF";

static const char *uncompile_code_script = R"ADFAAF(
import os
import shutil
import tempfile
import marshal
import cPickle
import uncompyle2

def safe_code_path(filename):
    # Embedded filenames are originally Windows paths, but Linux must never
    # interpret drive roots or .. as an output path outside the work folder.
    filename = filename.replace('\\', '/')
    for marker in ('/script/', '/carbon/', '/eve/'):
        if marker in filename:
            filename = marker.strip('/') + '/' + filename.split(marker, 1)[1]
            break
    relative = os.path.normpath(filename).lstrip('/')
    if (not relative or relative == '.' or relative == '..' or
        relative.startswith('../') or chr(0) in relative):
        raise ValueError("Unsafe embedded code filename: " + repr(filename))
    if ':' in relative.split('/')[0]:
        raise ValueError("Unexpected drive prefix in code filename: " + repr(filename))
    return relative

def unpack_code(input_file, destination):
    failures = 0
    with open(input_file, "rb") as handle:
        magic, payload, signature = cPickle.load(handle)
    if magic != 168686339:
        raise ValueError("Unsupported compiled.code magic: %r" % magic)
    code_entries = cPickle.loads(payload)['code']
    print "Unpacking %d code entries" % len(code_entries)
    for (original_name, filetype), (encrypted, sort_key) in code_entries:
        code = marshal.loads(blue.UnjumbleString(encrypted, True))
        filename = safe_code_path(code.co_filename)
        out_file = os.path.join(destination, os.path.splitext(filename)[0] + ".py")
        folder = os.path.dirname(out_file)
        if not os.path.isdir(folder):
            os.makedirs(folder)
        try:
            with open(out_file, "wb") as output:
                try:
                    uncompyle2.uncompyle('2.7', code, output)
                except Exception as original_error:
                    print "uncompyle2 failed for %s; trying uncompyle6: %s" % (
                        filename, original_error)
                    output.seek(0)
                    output.truncate()
                    from uncompyle6.main import decompile
                    decompile(code, out=output)
            print "Decompiled: " + filename
        except Exception as err:
            failures += 1
            print "Failed to decompile %s: %s" % (filename, err)
            if os.path.exists(out_file):
                os.remove(out_file)
    return failures

import blue
target = os.path.abspath(output_path)
if os.path.exists(target):
    raise IOError("Output directory already exists; choose a new -o or back it up: " + target)
parent = os.path.dirname(target)
if not os.path.isdir(parent):
    os.makedirs(parent)
stage = tempfile.mkdtemp(prefix=".evecc-unpack-", dir=parent)
try:
    count_failed = unpack_code(input_path, stage)
    os.rename(stage, target)
    if count_failed:
        raise RuntimeError("%d scripts could not be decompiled; partial output saved to %s"
                           % (count_failed, target))
except:
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    raise
)ADFAAF";

static const char *compile_lib_script = R"ADFAAF(
import zipfile
import os
import blue
import marshal
import time
import py_compile

import tempfile
import os.path

def PackCode(path, out):
     with zipfile.ZipFile(out, "w") as zf:
        for root, dirs, files in os.walk(path):
            for file in files:
                filename = os.path.join(root, file)
                if os.path.splitext(filename)[1] != ".py":
                    print "Skipping " + filename
                    continue
                newfilename = os.path.splitext(filename)[0] + ".pyj"
                newfilename = newfilename.split(path)[1][1:]
                print newfilename
                f = open(filename, "r")
                #data = f.read()
                code2 = compile(f.read(), filename.encode("utf-8"), "exec")
                #data = blue.crypto.JumbleString(f.read(), True)
                base_data = marshal.dumps(code2)
                data = blue.JumbleString(base_data, True)
                test = blue.UnjumbleString(data, True)
                if test != base_data:
                    raise Exception("Data mismatch")
                zf.writestr(newfilename.encode('cp1252'), data)
                f.close()

PackCode(input_path, output_path)
)ADFAAF";

static const char *compile_code_script = R"ADFAAF(
print input_paths
print output_path

import cPickle
import sys
import imp
import blue
import marshal
import os
import binascii
import struct

def ScriptName(path):
    normalized = os.path.abspath(path).replace('\\', '/')
    if '/script/' in normalized:
        return normalized.split('/script/', 1)[1]
    for component in ('eve', 'evedevtools', 'eveservices'):
        marker = '/' + component + '/'
        if marker in normalized:
            return component + '/' + normalized.split(marker, 1)[1]
    return os.path.basename(normalized)

def CarbonName(path):
    normalized = os.path.abspath(path).replace('\\', '/')
    if '/carbon/' not in normalized:
        raise ValueError("Expected source file beneath carbon/: " + normalized)
    return "../carbon/" + normalized.split('/carbon/', 1)[1]

def FindCodeFiles(root, files, isCarbon, base, idx):
    d = []
    i = 0
    for v in files:
        filename = os.path.join(root, v)
        name = ""
        if '/carbon/' in os.path.abspath(filename).replace('\\', '/'):
            name = "root:/" + CarbonName(filename)
        else:
            name = "script:/" + ScriptName(filename)
        sort_key = (idx * 10000) + i

        if os.path.splitext(filename)[1] != ".py":
            continue
        cache = blue.GetBuildCache(filename)
        data = None
        if cache == None:
            f = open(filename, "r")
            source = f.read()
            f.close()
            print "> Compiling " + filename + " as " + name + " with sort key " + str(sort_key)
            code = compile(source, filename.encode("utf8"), 'exec')
            data = blue.JumbleString(marshal.dumps(code), True)
            blue.SetBuildCache(filename, data)
        else:
            #print " > Using cached " + filename + " as " + name + " with sort key " + str(sort_key)
            data = cache

        try:
            d.append(((name, "compiled"), (data, sort_key)))
        except Exception as e:
            print e
            #import traceback
            #traceback.print_exc()
        i = i + 1
    return d

def Generate(base, output):
    #print "Generating code from " + base
    all = {}
    all['code'] = []

    idx = 0
    for path in base:
        print "Checking " + path
        for root, dirs, files in os.walk(path):
            dirs.sort()
            files.sort()
            all['code'] = all['code'] + FindCodeFiles(root, files, False, path, idx)
            idx = idx + 1

    all['cargo'] = {}
    all['preprocessed'] = False
    all['timestamp'] = "shit"
    all['code'] = sorted(all['code'], key=lambda x: x[1][1])

    print "Generating signature"
    allout = cPickle.dumps(all, -1)
    try:
        signature = blue.SignData(allout)
    except IOError as e:
        print e
        print "You probably don't have GoldenCD.pikl ;)"
    print "Signature: " + binascii.b2a_hex(signature)


    dataout = (168686339, allout, signature)
    d = cPickle.dumps(dataout)
    print "Writing " + output + " to ./" + output
    f = open(os.path.join(output_path), "wb")
    f.write(d)
    f.close()

try:
    Generate(input_paths, output_path)
except Exception as e:
    print e
    raise

)ADFAAF";
