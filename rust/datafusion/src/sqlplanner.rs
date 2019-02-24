// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

//! SQL Query Planner (produces logical plan from SQL AST)

use std::collections::HashSet;
use std::rc::Rc;
use std::string::String;
use std::sync::Arc;

use super::execution::error::*;
use super::logicalplan::*;

use arrow::datatypes::*;

use sqlparser::sqlast::*;

pub trait SchemaProvider {
    fn get_table_meta(&self, name: &str) -> Option<Arc<Schema>>;
    fn get_function_meta(&self, name: &str) -> Option<Arc<FunctionMeta>>;
}

/// SQL query planner
pub struct SqlToRel {
    schema_provider: Rc<SchemaProvider>,
}

impl SqlToRel {
    /// Create a new query planner
    pub fn new(schema_provider: Rc<SchemaProvider>) -> Self {
        SqlToRel { schema_provider }
    }

    /// Generate a logic plan from a SQL AST node
    pub fn sql_to_rel(&self, sql: &ASTNode) -> Result<Rc<LogicalPlan>> {
        match sql {
            &ASTNode::SQLSelect {
                ref projection,
                ref relation,
                ref selection,
                ref order_by,
                ref limit,
                ref group_by,
                ref having,
                ..
            } => {
                // parse the input relation so we have access to the row type
                let input = match relation {
                    &Some(ref r) => self.sql_to_rel(r)?,
                    &None => Rc::new(LogicalPlan::EmptyRelation {
                        schema: Arc::new(Schema::empty()),
                    }),
                };

                let input_schema = input.schema();

                // selection first
                let selection_plan = match selection {
                    &Some(ref filter_expr) => Some(LogicalPlan::Selection {
                        expr: self.sql_to_rex(&filter_expr, &input_schema.clone())?,
                        input: input.clone(),
                    }),
                    _ => None,
                };

                let expr: Vec<Expr> = projection
                    .iter()
                    .map(|e| self.sql_to_rex(&e, &input_schema))
                    .collect::<Result<Vec<Expr>>>()?;

                // collect aggregate expressions
                let aggr_expr: Vec<Expr> = expr
                    .iter()
                    .filter(|e| match e {
                        Expr::AggregateFunction { .. } => true,
                        _ => false,
                    })
                    .map(|e| e.clone())
                    .collect();

                if aggr_expr.len() > 0 {
                    let aggregate_input: Rc<LogicalPlan> = match selection_plan {
                        Some(s) => Rc::new(s),
                        _ => input.clone(),
                    };

                    let group_expr: Vec<Expr> = match group_by {
                        Some(gbe) => gbe
                            .iter()
                            .map(|e| self.sql_to_rex(&e, &input_schema))
                            .collect::<Result<Vec<Expr>>>()?,
                        None => vec![],
                    };
                    //println!("GROUP BY: {:?}", group_expr);

                    let mut all_fields: Vec<Expr> = group_expr.clone();
                    aggr_expr.iter().for_each(|x| all_fields.push(x.clone()));

                    let aggr_schema =
                        Schema::new(exprlist_to_fields(&all_fields, input_schema));

                    //TODO: selection, projection, everything else
                    Ok(Rc::new(LogicalPlan::Aggregate {
                        input: aggregate_input,
                        group_expr,
                        aggr_expr,
                        schema: Arc::new(aggr_schema),
                    }))
                } else {
                    let projection_input: Rc<LogicalPlan> = match selection_plan {
                        Some(s) => Rc::new(s),
                        _ => input.clone(),
                    };

                    let projection_schema = Arc::new(Schema::new(exprlist_to_fields(
                        &expr,
                        input_schema.as_ref(),
                    )));

                    let projection = LogicalPlan::Projection {
                        expr: expr,
                        input: projection_input,
                        schema: projection_schema.clone(),
                    };

                    if let &Some(_) = having {
                        return Err(ExecutionError::General(
                            "HAVING is not implemented yet".to_string(),
                        ));
                    }

                    let order_by_plan = match order_by {
                        &Some(ref order_by_expr) => {
                            let input_schema = projection.schema();
                            let order_by_rex: Result<Vec<Expr>> = order_by_expr
                                .iter()
                                .map(|e| {
                                    Ok(Expr::Sort {
                                        expr: Rc::new(
                                            self.sql_to_rex(&e.expr, &input_schema)
                                                .unwrap(),
                                        ),
                                        asc: e.asc,
                                    })
                                })
                                .collect();

                            LogicalPlan::Sort {
                                expr: order_by_rex?,
                                input: Rc::new(projection.clone()),
                                schema: input_schema.clone(),
                            }
                        }
                        _ => projection,
                    };

                    let limit_plan = match limit {
                        &Some(ref limit_expr) => {
                            let input_schema = order_by_plan.schema();
                            let limit_rex =
                                self.sql_to_rex(&limit_expr, &input_schema.clone())?;

                            LogicalPlan::Limit {
                                expr: limit_rex,
                                input: Rc::new(order_by_plan.clone()),
                                schema: input_schema.clone(),
                            }
                        }
                        _ => order_by_plan,
                    };

                    Ok(Rc::new(limit_plan))
                }
            }

            &ASTNode::SQLIdentifier(ref id) => {
                match self.schema_provider.get_table_meta(id.as_ref()) {
                    Some(schema) => Ok(Rc::new(LogicalPlan::TableScan {
                        schema_name: String::from("default"),
                        table_name: id.clone(),
                        schema: schema.clone(),
                        projection: None,
                    })),
                    None => Err(ExecutionError::General(format!(
                        "no schema found for table {}",
                        id
                    ))),
                }
            }

            _ => Err(ExecutionError::ExecutionError(format!(
                "sql_to_rel does not support this relation: {:?}",
                sql
            ))),
        }
    }

    /// Generate a relational expression from a SQL expression
    pub fn sql_to_rex(&self, sql: &ASTNode, schema: &Schema) -> Result<Expr> {
        match sql {
            &ASTNode::SQLValue(sqlparser::sqlast::Value::Long(n)) => {
                Ok(Expr::Literal(ScalarValue::Int64(n)))
            }
            &ASTNode::SQLValue(sqlparser::sqlast::Value::Double(n)) => {
                Ok(Expr::Literal(ScalarValue::Float64(n)))
            }
            &ASTNode::SQLValue(sqlparser::sqlast::Value::SingleQuotedString(ref s)) => {
                Ok(Expr::Literal(ScalarValue::Utf8(Rc::new(s.clone()))))
            }

            &ASTNode::SQLIdentifier(ref id) => {
                match schema.fields().iter().position(|c| c.name().eq(id)) {
                    Some(index) => Ok(Expr::Column(index)),
                    None => Err(ExecutionError::ExecutionError(format!(
                        "Invalid identifier '{}' for schema {}",
                        id,
                        schema.to_string()
                    ))),
                }
            }

            &ASTNode::SQLWildcard => {
                //                schema.columns().iter().enumerate()
                //                    .map(|(i,c)| Ok(Expr::Column(i))).collect()
                unimplemented!("SQL wildcard operator is not supported in projection - please use explicit column names")
            }

            &ASTNode::SQLCast {
                ref expr,
                ref data_type,
            } => Ok(Expr::Cast {
                expr: Rc::new(self.sql_to_rex(&expr, schema)?),
                data_type: convert_data_type(data_type)?,
            }),

            &ASTNode::SQLIsNull(ref expr) => {
                Ok(Expr::IsNull(Rc::new(self.sql_to_rex(expr, schema)?)))
            }

            &ASTNode::SQLIsNotNull(ref expr) => {
                Ok(Expr::IsNotNull(Rc::new(self.sql_to_rex(expr, schema)?)))
            }

            &ASTNode::SQLBinaryExpr {
                ref left,
                ref op,
                ref right,
            } => {
                let operator = match op {
                    &SQLOperator::Gt => Operator::Gt,
                    &SQLOperator::GtEq => Operator::GtEq,
                    &SQLOperator::Lt => Operator::Lt,
                    &SQLOperator::LtEq => Operator::LtEq,
                    &SQLOperator::Eq => Operator::Eq,
                    &SQLOperator::NotEq => Operator::NotEq,
                    &SQLOperator::Plus => Operator::Plus,
                    &SQLOperator::Minus => Operator::Minus,
                    &SQLOperator::Multiply => Operator::Multiply,
                    &SQLOperator::Divide => Operator::Divide,
                    &SQLOperator::Modulus => Operator::Modulus,
                    &SQLOperator::And => Operator::And,
                    &SQLOperator::Or => Operator::Or,
                    &SQLOperator::Not => Operator::Not,
                    &SQLOperator::Like => Operator::Like,
                    &SQLOperator::NotLike => Operator::NotLike,
                };

                let left_expr = self.sql_to_rex(&left, &schema)?;
                let right_expr = self.sql_to_rex(&right, &schema)?;
                let left_type = left_expr.get_type(schema);
                let right_type = right_expr.get_type(schema);

                match get_supertype(&left_type, &right_type) {
                    Some(supertype) => Ok(Expr::BinaryExpr {
                        left: Rc::new(left_expr.cast_to(&supertype, schema)?),
                        op: operator,
                        right: Rc::new(right_expr.cast_to(&supertype, schema)?),
                    }),
                    None => {
                        return Err(ExecutionError::General(format!(
                            "No common supertype found for binary operator {:?} \
                             with input types {:?} and {:?}",
                            operator, left_type, right_type
                        )));
                    }
                }
            }

            //            &ASTNode::SQLOrderBy { ref expr, asc } => Ok(Expr::Sort {
            //                expr: Rc::new(self.sql_to_rex(&expr, &schema)?),
            //                asc,
            //            }),
            &ASTNode::SQLFunction { ref id, ref args } => {
                //TODO: fix this hack
                match id.to_lowercase().as_ref() {
                    "min" | "max" | "sum" | "avg" => {
                        let rex_args = args
                            .iter()
                            .map(|a| self.sql_to_rex(a, schema))
                            .collect::<Result<Vec<Expr>>>()?;

                        // return type is same as the argument type for these aggregate
                        // functions
                        let return_type = rex_args[0].get_type(schema).clone();

                        Ok(Expr::AggregateFunction {
                            name: id.clone(),
                            args: rex_args,
                            return_type,
                        })
                    }
                    "count" => {
                        let rex_args = args
                            .iter()
                            .map(|a| match a {
                                // this feels hacky but translate COUNT(1)/COUNT(*) to
                                // COUNT(first_column)
                                ASTNode::SQLValue(sqlparser::sqlast::Value::Long(1)) => {
                                    Ok(Expr::Column(0))
                                }
                                ASTNode::SQLWildcard => Ok(Expr::Column(0)),
                                _ => self.sql_to_rex(a, schema),
                            })
                            .collect::<Result<Vec<Expr>>>()?;

                        Ok(Expr::AggregateFunction {
                            name: id.clone(),
                            args: rex_args,
                            return_type: DataType::UInt64,
                        })
                    }
                    _ => match self.schema_provider.get_function_meta(id) {
                        Some(fm) => {
                            let rex_args = args
                                .iter()
                                .map(|a| self.sql_to_rex(a, schema))
                                .collect::<Result<Vec<Expr>>>()?;

                            let mut safe_args: Vec<Expr> = vec![];
                            for i in 0..rex_args.len() {
                                safe_args.push(
                                    rex_args[i]
                                        .cast_to(fm.args()[i].data_type(), schema)?,
                                );
                            }

                            Ok(Expr::ScalarFunction {
                                name: id.clone(),
                                args: safe_args,
                                return_type: fm.return_type().clone(),
                            })
                        }
                        _ => Err(ExecutionError::General(format!(
                            "Invalid function '{}'",
                            id
                        ))),
                    },
                }
            }

            _ => Err(ExecutionError::General(format!(
                "Unsupported ast node {:?} in sqltorel",
                sql
            ))),
        }
    }
}

/// Convert SQL data type to relational representation of data type
pub fn convert_data_type(sql: &SQLType) -> Result<DataType> {
    match sql {
        SQLType::Boolean => Ok(DataType::Boolean),
        SQLType::SmallInt => Ok(DataType::Int16),
        SQLType::Int => Ok(DataType::Int32),
        SQLType::BigInt => Ok(DataType::Int64),
        SQLType::Float(_) | SQLType::Real => Ok(DataType::Float64),
        SQLType::Double => Ok(DataType::Float64),
        SQLType::Char(_) | SQLType::Varchar(_) => Ok(DataType::Utf8),
        other => Err(ExecutionError::NotImplemented(format!(
            "Unsupported SQL type {:?}",
            other
        ))),
    }
}

pub fn expr_to_field(e: &Expr, input_schema: &Schema) -> Field {
    match e {
        Expr::Column(i) => input_schema.fields()[*i].clone(),
        Expr::Literal(ref lit) => Field::new("lit", lit.get_datatype(), true),
        Expr::ScalarFunction {
            ref name,
            ref return_type,
            ..
        } => Field::new(name, return_type.clone(), true),
        Expr::AggregateFunction {
            ref name,
            ref return_type,
            ..
        } => Field::new(name, return_type.clone(), true),
        Expr::Cast { ref data_type, .. } => Field::new("cast", data_type.clone(), true),
        Expr::BinaryExpr {
            ref left,
            ref right,
            ..
        } => {
            let left_type = left.get_type(input_schema);
            let right_type = right.get_type(input_schema);
            Field::new(
                "binary_expr",
                get_supertype(&left_type, &right_type).unwrap(),
                true,
            )
        }
        _ => unimplemented!("Cannot determine schema type for expression {:?}", e),
    }
}

pub fn exprlist_to_fields(expr: &Vec<Expr>, input_schema: &Schema) -> Vec<Field> {
    expr.iter()
        .map(|e| expr_to_field(e, input_schema))
        .collect()
}

fn collect_expr(e: &Expr, accum: &mut HashSet<usize>) {
    match e {
        Expr::Column(i) => {
            accum.insert(*i);
        }
        Expr::Cast { ref expr, .. } => collect_expr(expr, accum),
        Expr::Literal(_) => {}
        Expr::IsNotNull(ref expr) => collect_expr(expr, accum),
        Expr::IsNull(ref expr) => collect_expr(expr, accum),
        Expr::BinaryExpr {
            ref left,
            ref right,
            ..
        } => {
            collect_expr(left, accum);
            collect_expr(right, accum);
        }
        Expr::AggregateFunction { ref args, .. } => {
            args.iter().for_each(|e| collect_expr(e, accum));
        }
        Expr::ScalarFunction { ref args, .. } => {
            args.iter().for_each(|e| collect_expr(e, accum));
        }
        Expr::Sort { ref expr, .. } => collect_expr(expr, accum),
    }
}

pub fn push_down_projection(
    plan: &Rc<LogicalPlan>,
    projection: &HashSet<usize>,
) -> Rc<LogicalPlan> {
    //println!("push_down_projection() projection={:?}", projection);
    match plan.as_ref() {
        LogicalPlan::Aggregate {
            ref input,
            ref group_expr,
            ref aggr_expr,
            ref schema,
        } => {
            //TODO: apply projection first
            let mut accum: HashSet<usize> = HashSet::new();
            group_expr.iter().for_each(|e| collect_expr(e, &mut accum));
            aggr_expr.iter().for_each(|e| collect_expr(e, &mut accum));
            Rc::new(LogicalPlan::Aggregate {
                input: push_down_projection(&input, &accum),
                group_expr: group_expr.clone(),
                aggr_expr: aggr_expr.clone(),
                schema: schema.clone(),
            })
        }
        LogicalPlan::Selection {
            ref expr,
            ref input,
        } => {
            let mut accum: HashSet<usize> = projection.clone();
            collect_expr(expr, &mut accum);
            Rc::new(LogicalPlan::Selection {
                expr: expr.clone(),
                input: push_down_projection(&input, &accum),
            })
        }
        LogicalPlan::TableScan {
            ref schema_name,
            ref table_name,
            ref schema,
            ..
        } => Rc::new(LogicalPlan::TableScan {
            schema_name: schema_name.to_string(),
            table_name: table_name.to_string(),
            schema: schema.clone(),
            projection: Some(projection.iter().cloned().collect()),
        }),
        LogicalPlan::Projection { .. } => plan.clone(),
        LogicalPlan::Sort { .. } => plan.clone(),
        LogicalPlan::Limit { .. } => plan.clone(),
        LogicalPlan::EmptyRelation { .. } => plan.clone(),
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use sqlparser::sqlparser::*;

    #[test]
    fn select_no_relation() {
        quick_test(
            "SELECT 1",
            "Projection: Int64(1)\
             \n  EmptyRelation",
        );
    }

    #[test]
    fn select_scalar_func_with_literal_no_relation() {
        quick_test(
            "SELECT sqrt(9)",
            "Projection: sqrt(CAST(Int64(9) AS Float64))\
             \n  EmptyRelation",
        );
    }

    #[test]
    fn select_simple_selection() {
        let sql = "SELECT id, first_name, last_name \
                   FROM person WHERE state = 'CO'";
        let expected = "Projection: #0, #1, #2\
                        \n  Selection: #4 Eq Utf8(\"CO\")\
                        \n    TableScan: person projection=None";
        quick_test(sql, expected);
    }

    #[test]
    fn select_compound_selection() {
        let sql = "SELECT id, first_name, last_name \
                   FROM person WHERE state = 'CO' AND age >= 21 AND age <= 65";
        let expected =
            "Projection: #0, #1, #2\
            \n  Selection: #4 Eq Utf8(\"CO\") And CAST(#3 AS Int64) GtEq Int64(21) And CAST(#3 AS Int64) LtEq Int64(65)\
            \n    TableScan: person projection=None";
        quick_test(sql, expected);
    }

    #[test]
    fn select_all_boolean_operators() {
        let sql = "SELECT age, first_name, last_name \
                   FROM person \
                   WHERE age = 21 \
                   AND age != 21 \
                   AND age > 21 \
                   AND age >= 21 \
                   AND age < 65 \
                   AND age <= 65";
        let expected = "Projection: #3, #1, #2\
                        \n  Selection: CAST(#3 AS Int64) Eq Int64(21) \
                        And CAST(#3 AS Int64) NotEq Int64(21) \
                        And CAST(#3 AS Int64) Gt Int64(21) \
                        And CAST(#3 AS Int64) GtEq Int64(21) \
                        And CAST(#3 AS Int64) Lt Int64(65) \
                        And CAST(#3 AS Int64) LtEq Int64(65)\
                        \n    TableScan: person projection=None";
        quick_test(sql, expected);
    }

    #[test]
    fn select_simple_aggregate() {
        quick_test(
            "SELECT MIN(age) FROM person",
            "Aggregate: groupBy=[[]], aggr=[[MIN(#3)]]\
             \n  TableScan: person projection=None",
        );
    }

    #[test]
    fn test_sum_aggregate() {
        quick_test(
            "SELECT SUM(age) from person",
            "Aggregate: groupBy=[[]], aggr=[[SUM(#3)]]\
             \n  TableScan: person projection=None",
        );
    }

    #[test]
    fn select_simple_aggregate_with_groupby() {
        quick_test(
            "SELECT state, MIN(age), MAX(age) FROM person GROUP BY state",
            "Aggregate: groupBy=[[#4]], aggr=[[MIN(#3), MAX(#3)]]\
             \n  TableScan: person projection=None",
        );
    }

    #[test]
    fn select_count_one() {
        let sql = "SELECT COUNT(1) FROM person";
        let expected = "Aggregate: groupBy=[[]], aggr=[[COUNT(#0)]]\
                        \n  TableScan: person projection=None";
        quick_test(sql, expected);
    }

    #[test]
    fn select_scalar_func() {
        let sql = "SELECT sqrt(age) FROM person";
        let expected = "Projection: sqrt(CAST(#3 AS Float64))\
                        \n  TableScan: person projection=None";
        quick_test(sql, expected);
    }

    #[test]
    fn select_order_by() {
        let sql = "SELECT id FROM person ORDER BY id";
        let expected = "Sort: #0 ASC\
                        \n  Projection: #0\
                        \n    TableScan: person projection=None";
        quick_test(sql, expected);
    }

    #[test]
    fn select_order_by_desc() {
        let sql = "SELECT id FROM person ORDER BY id DESC";
        let expected = "Sort: #0 DESC\
                        \n  Projection: #0\
                        \n    TableScan: person projection=None";
        quick_test(sql, expected);
    }

    #[test]
    fn test_collect_expr() {
        let mut accum: HashSet<usize> = HashSet::new();
        collect_expr(
            &Expr::Cast {
                expr: Rc::new(Expr::Column(3)),
                data_type: DataType::Float64,
            },
            &mut accum,
        );
        collect_expr(
            &Expr::Cast {
                expr: Rc::new(Expr::Column(3)),
                data_type: DataType::Float64,
            },
            &mut accum,
        );
        println!("accum: {:?}", accum);
        assert_eq!(1, accum.len());
        assert!(accum.contains(&3));
    }

    /// Create logical plan, write with formatter, compare to expected output
    fn quick_test(sql: &str, expected: &str) {
        use sqlparser::dialect::*;
        let dialect = GenericSqlDialect {};
        let planner = SqlToRel::new(Rc::new(MockSchemaProvider {}));
        let ast = Parser::parse_sql(&dialect, sql.to_string()).unwrap();
        let plan = planner.sql_to_rel(&ast).unwrap();
        assert_eq!(expected, format!("{:?}", plan));
    }

    struct MockSchemaProvider {}

    impl SchemaProvider for MockSchemaProvider {
        fn get_table_meta(&self, name: &str) -> Option<Arc<Schema>> {
            match name {
                "person" => Some(Arc::new(Schema::new(vec![
                    Field::new("id", DataType::UInt32, false),
                    Field::new("first_name", DataType::Utf8, false),
                    Field::new("last_name", DataType::Utf8, false),
                    Field::new("age", DataType::Int32, false),
                    Field::new("state", DataType::Utf8, false),
                    Field::new("salary", DataType::Float64, false),
                ]))),
                _ => None,
            }
        }

        fn get_function_meta(&self, name: &str) -> Option<Arc<FunctionMeta>> {
            match name {
                "sqrt" => Some(Arc::new(FunctionMeta::new(
                    "sqrt".to_string(),
                    vec![Field::new("n", DataType::Float64, false)],
                    DataType::Float64,
                    FunctionType::Scalar,
                ))),
                _ => None,
            }
        }
    }

}
