#ifndef SERIAL_HPP
#define SERIAL_HPP

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <atomic>
#include <sys/stat.h>
#include "geometry.hpp"
#include "mbtiles.hpp"
#include "jsonpull/jsonpull.h"

size_t fwrite_check(const void *ptr, size_t size, size_t nitems, FILE *stream, std::atomic<long long> *fpos, const char *fname);

void serialize_int(FILE *out, int n, std::atomic<long long> *fpos, const char *fname);
void serialize_long_long(FILE *out, long long n, std::atomic<long long> *fpos, const char *fname);
void serialize_ulong_long(FILE *out, unsigned long long n, std::atomic<long long> *fpos, const char *fname);
void serialize_byte(FILE *out, signed char n, std::atomic<long long> *fpos, const char *fname);
void serialize_uint(FILE *out, unsigned n, std::atomic<long long> *fpos, const char *fname);

void serialize_int(std::string &out, int n);
void serialize_long_long(std::string &out, long long n);
void serialize_ulong_long(std::string &out, unsigned long long n);
void serialize_byte(std::string &out, signed char n);
void serialize_uint(std::string &out, unsigned n);

void deserialize_int(char **f, int *n);
void deserialize_long_long(char **f, long long *n);
void deserialize_ulong_long(char **f, unsigned long long *n);
void deserialize_uint(char **f, unsigned *n);
void deserialize_byte(char **f, signed char *n);

// This is the main representation of attribute values in memory and
// in the string pool. The type is one of the mvt_value type (mvt_string,
// mvt_double, mvt_bool, or mvt_null). Note that all numeric values,
// whether integer or floating point, use mvt_double here.
struct serial_val {
	int type = 0;
	std::string s = "";
};

struct serial_feature {
	long long layer = 0;
	int segment = 0;
	long long seq = 0;

	signed char t = 0;
	signed char feature_minzoom = 0;

	bool has_id = false;
	unsigned long long id = 0;

	bool has_tippecanoe_minzoom = false;
	int tippecanoe_minzoom = 0;

	bool has_tippecanoe_maxzoom = false;
	int tippecanoe_maxzoom = 0;

	drawvec geometry = drawvec();
	unsigned long long index = 0;
	unsigned long long label_point = 0;
	long long extent = 0;

	std::vector<long long> keys{};
	std::vector<long long> values{};

	// XXX This isn't serialized. Should it be here?
	long long bbox[4] = {0, 0, 0, 0};
	std::vector<std::string> full_keys{};
	std::vector<serial_val> full_values{};
	std::string layername = "";
	bool dropped = false;
	drawvec edge_nodes;
};

std::string serialize_feature(serial_feature *sf, long long wx, long long wy);
serial_feature deserialize_feature(std::string &geoms, unsigned z, unsigned tx, unsigned ty, unsigned *initial_x, unsigned *initial_y);

struct reader {
	int poolfd = -1;
	int treefd = -1;
	int geomfd = -1;
	int indexfd = -1;
	int vertexfd = -1;
	int nodefd = -1;

	struct memfile *poolfile = NULL;
	struct memfile *treefile = NULL;
	FILE *geomfile = NULL;
	FILE *indexfile = NULL;
	FILE *vertexfile = NULL;
	FILE *nodefile = NULL;

	std::atomic<long long> geompos;
	std::atomic<long long> indexpos;
	std::atomic<long long> vertexpos;
	std::atomic<long long> nodepos;

	long long file_bbox[4] = {0, 0, 0, 0};

	long long file_bbox1[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0, 0};	      // standard -180 to 180 world plane
	long long file_bbox2[4] = {0x1FFFFFFFF, 0xFFFFFFFF, 0x100000000, 0};  // 0 to 360 world plane

	struct stat geomst {};

	char *geom_map = NULL;

	reader()
	    : geompos(0), indexpos(0), vertexpos(0), nodepos(0) {
	}

	reader(reader const &r) {
		poolfd = r.poolfd;
		treefd = r.treefd;
		geomfd = r.geomfd;
		indexfd = r.indexfd;
		vertexfd = r.vertexfd;
		nodefd = r.nodefd;

		poolfile = r.poolfile;
		treefile = r.treefile;
		geomfile = r.geomfile;
		indexfile = r.indexfile;
		vertexfile = r.vertexfile;
		nodefile = r.nodefile;

		long long p = r.geompos;
		geompos = p;

		p = r.indexpos;
		indexpos = p;

		p = r.vertexpos;
		vertexpos = p;

		p = r.nodepos;
		nodepos = p;

		memcpy(file_bbox, r.file_bbox, sizeof(file_bbox));

		geomst = r.geomst;

		geom_map = r.geom_map;
	}
};

struct serialization_state {
	const char *fname = NULL;  // source file name
	int line = 0;		   // user-oriented location within source for error reports

	std::atomic<long long> *layer_seq = NULL;     // sequence within current layer
	std::atomic<long long> *progress_seq = NULL;  // overall sequence for progress indicator

	std::vector<struct reader> *readers = NULL;  // array of data for each input thread
	int segment = 0;			     // the current input thread

	unsigned *initial_x = NULL;  // relative offset of all geometries
	unsigned *initial_y = NULL;
	int *initialized = NULL;

	double *dist_sum = NULL;  // running tally for calculation of resolution within features
	size_t *dist_count = NULL;
	double *area_sum = NULL;
	bool want_dist = false;

	int maxzoom = 0;
	int basezoom = 0;

	bool filters = false;
	bool uses_gamma = false;

	std::map<std::string, layermap_entry> *layermap = NULL;

	std::map<std::string, int> const *attribute_types = NULL;
	std::set<std::string> *exclude = NULL;
	std::set<std::string> *include = NULL;
	int exclude_all = 0;
};

struct vertex {
	// these are scaled geometry,
	// but because scaling is disabled if P_SHARED_NODES is set,
	// they are effectively also world coordinates
	draw p1;
	draw mid;
	draw p2;

	vertex(draw one, draw joint, draw two) {
		if (one < two) {
			p1 = one;
			p2 = two;
		} else {
			p1 = two;
			p2 = one;
		}

		mid = joint;
	}

	bool operator<(const vertex &v) const {
		if (mid < v.mid) {
			return true;
		} else if (mid == v.mid) {
			if (p1 < v.p1) {
				return true;
			} else if (p1 == v.p1) {
				if (p2 < v.p2) {
					return true;
				}
			}
		}

		return false;
	}
};

struct node {
	// this is in quadkey coordinates so that the nodes for each tile
	// will be adjacent in memory, reducing potential thrashing during
	// the binary search.
	unsigned long long index;
};

int nodecmp(const void *void1, const void *void2);

int serialize_feature(struct serialization_state *sst, serial_feature &sf);
void coerce_value(std::string const &key, int &vt, std::string &val, std::map<std::string, int> const *attribute_types);

#endif
