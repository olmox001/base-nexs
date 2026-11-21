/* Full NXOLMO base/types contract, moved to the NEXS canonical ABI. */
#include "include/nexs_types.h"

int nx_object_id_equal(nx_object_id left, nx_object_id right) {
  return left.high == right.high && left.low == right.low;
}
int nx_object_id_is_zero(nx_object_id value) {
  return value.high == 0U && value.low == 0U;
}
int nx_object_ref_equal(nx_object_ref left, nx_object_ref right) {
  return nx_object_id_equal(left.id, right.id) &&
         left.generation == right.generation;
}
int nx_object_ref_is_valid(nx_object_ref value) {
  return !nx_object_id_is_zero(value.id) && value.generation != 0U;
}
