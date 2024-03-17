#include "correct/reed-solomon.h"
#include "correct/reed-solomon/field.h"

polynomial_t_rs polynomial_create(unsigned int order);
void polynomial_destroy(polynomial_t_rs polynomial);
void polynomial_mul(field_t field, polynomial_t_rs l, polynomial_t_rs r, polynomial_t_rs res);
void polynomial_mod(field_t field, polynomial_t_rs dividend, polynomial_t_rs divisor, polynomial_t_rs mod);
void polynomial_formal_derivative(field_t field, polynomial_t_rs poly, polynomial_t_rs der);
field_element_t polynomial_eval(field_t field, polynomial_t_rs poly, field_element_t val);
field_element_t polynomial_eval_lut(field_t field, polynomial_t_rs poly, const field_logarithm_t *val_exp);
field_element_t polynomial_eval_log_lut(field_t field, polynomial_t_rs poly_log, const field_logarithm_t *val_exp);
void polynomial_build_exp_lut(field_t field, field_element_t val, unsigned int order, field_logarithm_t *val_exp);
polynomial_t_rs polynomial_init_from_roots(field_t field, unsigned int nroots, field_element_t *roots, polynomial_t_rs poly, polynomial_t_rs *scratch);
polynomial_t_rs polynomial_create_from_roots(field_t field, unsigned int nroots, field_element_t *roots);
