#!/usr/bin/env python

import os
import math
import argparse
import xml.etree.ElementTree as ET
from pyproj import Proj, transform
from PIL import Image, ImageDraw

IMG_WIDTH  = 12800
IMG_HEIGHT = 12800
IMG_BORDER = 20

points = [ ]
total_length = 0


def distance(p1, p2):
    lon1 = math.radians(float(p1['lon']))
    lat1 = math.radians(float(p1['lat']))
    lon2 = math.radians(float(p2['lon']))
    lat2 = math.radians(float(p2['lat']))

    a = (lon2 - lon1) * math.cos((lat1 + lat2) / 2)
    b = (lat2 - lat1)
    r = 6363720 # Earth radius at ground level for latitude 55.751244

    return math.sqrt(a * a + b * b) * r


def read_gpx(gpx_filename):
    print("Processing %s" % gpx_filename, end='')

    try:
        tree = ET.ElementTree(file=gpx_filename)
    except FileNotFoundError as err:
        exit(err)
    except ET.ParseError as err:
        exit(err)

    ns = { 'gpx': 'http://www.topografix.com/GPX/1/1' }
    root = tree.getroot()
    if root.tag != '{%s}gpx' % ns['gpx']:
        exit("XML document of the wrong type, root node != gpx")

    trk_length = 0
    for trk in root.findall('gpx:trk', ns):
        for trkseg in trk.findall('gpx:trkseg', ns):
            trkseg_length = 0
            is_start = True                 # First point of the track segment

            for trkpt in trkseg.findall('gpx:trkpt', ns):
                p = trkpt.attrib
                p['terminal'] = is_start
                if not is_start:
                    trkseg_length = trkseg_length + distance(points[-1], p)
                points.append(p)
                is_start = False

            points[-1]['terminal'] = True   # Last point of the track segment
            trk_length = trk_length + trkseg_length

    print(" [%.2f km]" % (trk_length / 1000))
    global total_length
    total_length = total_length + trk_length


def read_dir(dir_name):
    for f in os.listdir(dir_name):
        if not f.startswith('.'):
            f = os.path.join(dir_name, f)
            if os.path.isfile(f):
                read_gpx(f)


def lonlat_to_xy():
    pj_src = Proj(ellps='WGS84', proj='latlong', datum='WGS84')
    pj_dst = Proj(ellps='WGS84', proj='utm', lon_0='39')    # Moscow

    for p in points:
        p['x'], p['y'] = transform(pj_src, pj_dst, p['lon'], p['lat'])

    min_x = min(p['x'] for p in points)
    max_x = max(p['x'] for p in points)
    min_y = min(p['y'] for p in points)
    max_y = max(p['y'] for p in points)
    k_x = (IMG_WIDTH  - 2 * IMG_BORDER) / (max_x - min_x)
    k_y = (IMG_HEIGHT - 2 * IMG_BORDER) / (max_y - min_y)
    k = min(k_x, k_y)

    for p in points:
        p['x'] = round(IMG_BORDER + k * (p['x'] - min_x))
        p['y'] = IMG_HEIGHT - round(IMG_BORDER + k * (p['y'] - min_y))


def draw_img(img_file_name):
    print("Creating image...")

    palette = [ 0,0,0, 0,255,0, 255,0,0 ]
    img = Image.new(mode='P', size=(IMG_WIDTH, IMG_HEIGHT))
    img.putpalette(palette)
    draw = ImageDraw.Draw(img)

    p2 = points[0]
    for p in points[1:]:
        p1 = p2
        p2 = p
        if not (p1['terminal'] and p2['terminal']):
            draw.line((p1['x'], p1['y'], p2['x'], p2['y']), fill=1)

    for p in points:
        if p['terminal']:
            draw.line((p['x'] - 1, p['y'], p['x'] + 1, p['y']), fill=2)
            draw.line((p['x'], p['y'] - 1, p['x'], p['y'] + 1), fill=2)

    del draw
    img.save(img_file_name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('track_list', help="List of GPX tracks or directories with tracks")
    parser.add_argument('out_image', help="Output PNG image")
    args = parser.parse_args()

    try:
        f = open(args.track_list)
    except FileNotFoundError as err:
        exit(err)

    for track in f:
        track = track.rstrip()
        if os.path.isdir(track):
            read_dir(track)
        else:
            read_gpx(track)
    f.close()

    print("Total: %.2f km\n" % (total_length / 1000))

    lonlat_to_xy()

    draw_img(args.out_image)


if __name__ == '__main__':
    main()
