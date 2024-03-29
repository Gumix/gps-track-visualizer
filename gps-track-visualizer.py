#!/usr/bin/env python3

import os
import math
import argparse
import xml.etree.ElementTree as ET
from pyproj import Transformer, CRS
from pyproj.aoi import AreaOfInterest
from pyproj.database import query_utm_crs_info
from PIL import Image, ImageDraw

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
    for f in sorted(os.listdir(dir_name)):
        if not f.startswith('.'):
            f = os.path.join(dir_name, f)
            if os.path.isfile(f):
                read_gpx(f)


def lonlat_to_xy(img_width, img_height, img_border):
    print("Converting coordinates...")

    utm_crs_list = query_utm_crs_info(
        datum_name = "WGS 84",
        area_of_interest = AreaOfInterest(
            west_lon_degree  = min(float(p['lon']) for p in points),
            south_lat_degree = min(float(p['lat']) for p in points),
            east_lon_degree  = max(float(p['lon']) for p in points),
            north_lat_degree = max(float(p['lat']) for p in points)))

    transformer = Transformer.from_crs(
        {"proj":'latlong', "ellps":'WGS84', "datum":'WGS84'},
        CRS.from_epsg(utm_crs_list[0].code),
        always_xy=True)

    for p in points:
        p['x'], p['y'] = transformer.transform(p['lon'], p['lat'])

    min_x = min(p['x'] for p in points)
    max_x = max(p['x'] for p in points)
    min_y = min(p['y'] for p in points)
    max_y = max(p['y'] for p in points)
    k_x = (img_width  - 2 * img_border) / (max_x - min_x)
    k_y = (img_height - 2 * img_border) / (max_y - min_y)
    k = min(k_x, k_y)

    for p in points:
        p['x'] = round(img_border + k * (p['x'] - min_x))
        p['y'] = img_height - round(img_border + k * (p['y'] - min_y))


def draw_img(img_file_name, img_width, img_height):
    print("Creating image...")

    palette = [ 0,0,0, 0,255,0, 255,0,0 ]
    img = Image.new(mode='P', size=(img_width, img_height))
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
    parser.add_argument('track_list', help="list of GPX tracks or directories with tracks")
    parser.add_argument('out_image',  help="output PNG image")
    parser.add_argument('--width',  type=int, default=12800, help="width of the output image")
    parser.add_argument('--height', type=int, default=12800, help="height of the output image")
    parser.add_argument('--border', type=int, default=20,    help="border on the output image")
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

    lonlat_to_xy(args.width, args.height, args.border)

    draw_img(args.out_image, args.width, args.height)


if __name__ == '__main__':
    main()
