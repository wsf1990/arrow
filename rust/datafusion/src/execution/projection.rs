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

//! Execution of a projection

use std::cell::RefCell;
use std::rc::Rc;
use std::sync::Arc;

use arrow::array::ArrayRef;
use arrow::datatypes::{Field, Schema};
use arrow::record_batch::RecordBatch;

use super::error::Result;
use super::expression::RuntimeExpr;
use super::relation::Relation;

pub struct ProjectRelation {
    schema: Arc<Schema>,
    input: Rc<RefCell<Relation>>,
    expr: Vec<RuntimeExpr>,
}

impl ProjectRelation {
    pub fn new(
        input: Rc<RefCell<Relation>>,
        expr: Vec<RuntimeExpr>,
        schema: Arc<Schema>,
    ) -> Self {
        ProjectRelation {
            input,
            expr,
            schema,
        }
    }
}

impl Relation for ProjectRelation {
    fn next(&mut self) -> Result<Option<RecordBatch>> {
        match self.input.borrow_mut().next()? {
            Some(batch) => {
                let projected_columns: Result<Vec<ArrayRef>> =
                    self.expr.iter().map(|e| e.get_func()(&batch)).collect();

                let schema = Schema::new(
                    self.expr
                        .iter()
                        .map(|e| Field::new(&e.get_name(), e.get_type(), true))
                        .collect(),
                );

                let projected_batch: RecordBatch =
                    RecordBatch::new(Arc::new(schema), projected_columns?);

                Ok(Some(projected_batch))
            }
            None => Ok(None),
        }
    }

    fn schema(&self) -> &Arc<Schema> {
        &self.schema
    }
}

#[cfg(test)]
mod tests {
    use super::super::super::logicalplan::Expr;
    use super::super::context::ExecutionContext;
    use super::super::datasource::CsvDataSource;
    use super::super::expression;
    use super::super::relation::DataSourceRelation;
    use super::*;
    use arrow::datatypes::{DataType, Field, Schema};

    #[test]
    fn project_first_column() {
        let schema = Arc::new(Schema::new(vec![
            Field::new("c1", DataType::Utf8, false),
            Field::new("c2", DataType::UInt32, false),
            Field::new("c3", DataType::Int8, false),
            Field::new("c3", DataType::Int16, false),
            Field::new("c4", DataType::Int32, false),
            Field::new("c5", DataType::Int64, false),
            Field::new("c6", DataType::UInt8, false),
            Field::new("c7", DataType::UInt16, false),
            Field::new("c8", DataType::UInt32, false),
            Field::new("c9", DataType::UInt64, false),
            Field::new("c10", DataType::Float32, false),
            Field::new("c11", DataType::Float64, false),
            Field::new("c12", DataType::Utf8, false),
        ]));

        let ds = CsvDataSource::new(
            "../../testing/data/csv/aggregate_test_100.csv",
            schema.clone(),
            true,
            &None,
            1024,
        );
        let relation = Rc::new(RefCell::new(DataSourceRelation::new(Rc::new(
            RefCell::new(ds),
        ))));
        let context = ExecutionContext::new();

        let projection_expr =
            vec![
                expression::compile_expr(&context, &Expr::Column(0), schema.as_ref())
                    .unwrap(),
            ];

        let mut projection = ProjectRelation::new(relation, projection_expr, schema);
        let batch = projection.next().unwrap().unwrap();
        assert_eq!(1, batch.num_columns());

        assert_eq!("c1", batch.schema().field(0).name());
    }

}
