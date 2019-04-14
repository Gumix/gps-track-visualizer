#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libxml/xmlreader.h>
#include <proj_api.h>
#include <gd.h>


enum { IMG_WIDTH  = 12800 };
enum { IMG_HEIGHT = 12800 };
enum { IMG_BORDER = 20 };
enum { MAX_POINTS = 1000000 };

struct point
{
	unsigned x, y;
	bool is_end;
};

static int count;
static int last_count;
static double total_length;

static double lon[MAX_POINTS];
static double lat[MAX_POINTS];

static struct point points[MAX_POINTS];


static void get_point_coordinates(xmlNodePtr trkpt)
{
	xmlChar *xml_lon = xmlGetProp(trkpt, (const xmlChar *) "lon");
	xmlChar *xml_lat = xmlGetProp(trkpt, (const xmlChar *) "lat");

	lon[count]   = atof((const char *) xml_lon);
	lat[count++] = atof((const char *) xml_lat);

	xmlFree(xml_lon);
	xmlFree(xml_lat);
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

		if (!points[i - 1].is_end || !points[i].is_end)
			len += distance(lon1, lat1, lon2, lat2);
	}

	return len;
}

static int read_gpx(const char *gpx_filename)
{
	printf("Processing %s", gpx_filename);

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
				points[count].is_end = true;
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
				points[count - 1].is_end = true;
				trkseg = trkseg->next;
			}
		}
		trk = trk->next;
	}

	xmlFreeDoc(doc);

	double length = calc_length(last_count, count - 1);
	last_count = count;
	total_length += length;
	printf(" [%.2f km]\n", length / 1000);

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

	pj_free(pj_src);
	pj_free(pj_dst);

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
		points[i].x = IMG_BORDER + k * (lon[i] - min_lon) + 0.5;
		points[i].y = IMG_BORDER + k * (lat[i] - min_lat) + 0.5;
	}

	return 0;
}

static int draw_img(const char *img_file_name)
{
	gdImagePtr img = gdImageCreate(IMG_WIDTH, IMG_HEIGHT);

	const int dot_r = 3;
	int black = gdImageColorAllocate(img, 0,   0, 0); // Set background color
	int green = gdImageColorAllocate(img, 0, 255, 0);
	int red   = gdImageColorAllocate(img, 255, 0, 0);

	struct point p2 = points[0];
	for (int i = 1; i < count; i++)
	{
		struct point p1 = p2;
		p2 = points[i];

		if (!p1.is_end || !p2.is_end)
			gdImageLine(img, p1.x, IMG_HEIGHT - p1.y, p2.x, IMG_HEIGHT - p2.y, green);
	}

	for (int i = 0; i < count; i++)
	{
		struct point p = points[i];
		if (p.is_end)
			gdImageFilledEllipse(img, p.x, IMG_HEIGHT - p.y, dot_r, dot_r, red);
	}

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

static int read_dir(const char *dir_name)
{
	DIR *dir = opendir(dir_name);
	if (!dir)
		return 1;

	struct dirent *ent;
	while ((ent = readdir(dir)))
		if (ent->d_name[0] != '.')
		{
			char track[256];
			strncpy(track, dir_name, sizeof(track) - 1);
			strncat(track, ent->d_name, sizeof(track) - strlen(track) - 1);

			if (read_gpx(track) != 0)
			{
				closedir(dir);
				return 2;
			}
		}

	closedir(dir);
	return 0;
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

		struct stat st;
		if (stat(track, &st) == 0 && S_ISDIR(st.st_mode))
		{
			if (read_dir(track) != 0)
			{
				fclose(file);
				return 3;
			}
		}
		else
		{
			if (read_gpx(track) != 0)
			{
				fclose(file);
				return 4;
			}
		}
	}
	fclose(file);

	printf("Total: %.2f km\n", total_length / 1000);

	if (lonlat_to_xy())
		return 5;

	if (draw_img(out_img))
		return 6;

	return 0;
}
