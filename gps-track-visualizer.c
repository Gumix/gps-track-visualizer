#include <stdbool.h>
#include <string.h>
#include <libxml/xmlreader.h>
#include <proj_api.h>
#include <gd.h>


enum { IMG_WIDTH  = 12800 };
enum { IMG_HEIGHT = 12800 };
enum { IMG_BORDER = 20 };
enum { MAX_POINTS = 1000000 };


static int count;
static int last_count;
static double total_length;

static double lon[MAX_POINTS];
static double lat[MAX_POINTS];

static int x[MAX_POINTS];
static int y[MAX_POINTS];

static bool ends[MAX_POINTS];


static void get_point_coordinates(xmlNodePtr trkpt)
{
	xmlChar *xml_lon = xmlGetProp(trkpt, (const xmlChar *) "lon");
	xmlChar *xml_lat = xmlGetProp(trkpt, (const xmlChar *) "lat");

	lon[count]   = atof((const char *) xml_lon);
	lat[count++] = atof((const char *) xml_lat);

	xmlFree(xml_lon);
	xmlFree(xml_lat);
}

static int read_gpx(const char *gpx_filename)
{
	printf("Processing %s\n", gpx_filename);

	xmlDocPtr doc;
	if (!(doc = xmlParseFile(gpx_filename)))
	{
		fprintf(stderr, "XML document not parsed successfully\n");
		return 1;
	}

	xmlNodePtr root, trk, trkseg, trkpt;
	if (!(root = xmlDocGetRootElement(doc)))
	{
		fprintf(stderr, "Empty XML document\n");
		xmlFreeDoc(doc);
		return 1;
	}

	if (xmlStrcmp(root->name, (const xmlChar *) "gpx"))
	{
		fprintf(stderr, "XML document of the wrong type, root node != gpx\n");
		xmlFreeDoc(doc);
		return 1;
	}

	trk = root->xmlChildrenNode;
	while (trk != NULL)
	{
		if (!xmlStrcmp(trk->name, (const xmlChar *) "trk"))
		{
			trkseg = trk->xmlChildrenNode;
			while (trkseg != NULL)
			{
				ends[count] = true;
				if (!xmlStrcmp(trkseg->name, (const xmlChar *) "trkseg"))
				{
					trkpt = trkseg->xmlChildrenNode;
					while (trkpt != NULL)
					{
						if (!xmlStrcmp(trkpt->name, (const xmlChar *) "trkpt"))
							get_point_coordinates(trkpt);

						if (count >= MAX_POINTS)
							return 1;

						trkpt = trkpt->next;
					}
				}
				ends[count - 1] = true;
				trkseg = trkseg->next;
			}
		}
		trk = trk->next;
	}

	xmlFreeDoc(doc);
	return 0;
}

static int lonlat_to_xy()
{
	projPJ pj_src, pj_dst;

	if (!(pj_src = pj_init_plus("+proj=latlong +ellps=WGS84 +datum=WGS84")))
		return 1;
	if (!(pj_dst = pj_init_plus("+proj=utm +ellps=WGS84 +lon_0=39"))) // Moscow
		return 1;

	for (int i = 0; i < count; i++)
	{
		lon[i] *= M_PI / 180.0;
		lat[i] *= M_PI / 180.0;
	}

	pj_transform(pj_src, pj_dst, count, 1, lon, lat, NULL);

	double min_lon = lon[0];
	double max_lon = lon[0];
	double min_lat = lat[0];
	double max_lat = lat[0];

	for (int i = 1; i < count; i++)
	{
		min_lon = (min_lon < lon[i]) ? min_lon : lon[i];
		max_lon = (max_lon > lon[i]) ? max_lon : lon[i];
		min_lat = (min_lat < lat[i]) ? min_lat : lat[i];
		max_lat = (max_lat > lat[i]) ? max_lat : lat[i];
	}

	double k_lon = (IMG_WIDTH  - 2 * IMG_BORDER) / (max_lon - min_lon);
	double k_lat = (IMG_HEIGHT - 2 * IMG_BORDER) / (max_lat - min_lat);

	double k = (k_lon < k_lat) ? k_lon : k_lat;

	for (int i = 0; i < count; i++)
	{
		x[i] = IMG_BORDER + k * (lon[i] - min_lon) + 0.5;
		y[i] = IMG_BORDER + k * (lat[i] - min_lat) + 0.5;
	}

	pj_free(pj_src);
	pj_free(pj_dst);

	return 0;
}

static int draw_img(const char *img_file_name)
{
	gdImagePtr img = gdImageCreate(IMG_WIDTH, IMG_HEIGHT);

	int black = gdImageColorAllocate(img, 0,   0, 0); // Set background color
	int green = gdImageColorAllocate(img, 0, 255, 0);
	int red   = gdImageColorAllocate(img, 255, 0, 0);

	int x2 = x[0];
	int y2 = y[0];
	for (int i = 1; i < count; i++)
	{
		int x1 = x2;
		int y1 = y2;
		x2 = x[i];
		y2 = y[i];

		if (!ends[i - 1] || !ends[i])
			gdImageLine(img, x1, IMG_HEIGHT - y1, x2, IMG_HEIGHT - y2, green);
	}

	for (int i = 0; i < count; i++)
		if (ends[i])
			gdImageFilledEllipse(img, x[i], IMG_HEIGHT - y[i], 3, 3, red);

	FILE *pngout = fopen(img_file_name, "wb");
	if (!pngout)
	{
		perror(img_file_name);
		return 1;
	}

	gdImagePng(img, pngout);
	fclose(pngout);
	gdImageDestroy(img);

	return 0;
}

static double distance(double lon1, double lat1, double lon2, double lat2)
{
	const int R = 6367500;

	lon1 *= M_PI / 180.0;
	lat1 *= M_PI / 180.0;
	lon2 *= M_PI / 180.0;
	lat2 *= M_PI / 180.0;

	double x = (lon2 - lon1) * cos((lat1 + lat2) / 2);
	double y = (lat2 - lat1);

	return sqrt(x * x + y * y) * R;
}

static double calc_length(int p1, int p2)
{
	double len = 0.0;

	double lon2 = lon[p1];
	double lat2 = lat[p1];
	for (int i = p1 + 1; i <= p2; i++)
	{
		double lon1 = lon2;
		double lat1 = lat2;
		lon2 = lon[i];
		lat2 = lat[i];

		if (!ends[i - 1] || !ends[i])
			len += distance(lon1, lat1, lon2, lat2);
	}

	return len;
}

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		fprintf(stderr, "Usage: %s track_list out_img\n", argv[0]);
		return 1;
	}

	const char *track_list = argv[1];
	const char *out_img    = argv[2];

	FILE *file = fopen(track_list, "r");
	if (!file)
	{
		perror(track_list);
		return 2;
	}

	char track[256];
	while (fgets(track, sizeof track, file) != NULL)
	{
		char *newline = strchr(track, '\n');
		if (newline)
			*newline = '\0';

		if (read_gpx(track))
		{
			fclose(file);
			return 3;
		}

		double length = calc_length(last_count, count - 1);
		last_count = count;
		total_length += length;

		printf(" [%.2f km]\n", length / 1000);
	}
	fclose(file);

	printf("Total: %.2f km\n", total_length / 1000);

	if (lonlat_to_xy())
		return 4;

	if (draw_img(out_img))
		return 5;

	return 0;
}
