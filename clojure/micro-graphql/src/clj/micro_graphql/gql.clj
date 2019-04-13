(ns micro-graphql.gql
  (:require
   [graphql-clj.executor :as executor]))

(def schema-str "type User {
    byName(s: String): String
    name: String
    age: Int
  }
  type QueryRoot {
    user: User
  }

  schema {
    query: QueryRoot
  }")

(defn resolver-fn [type-name field-name]
  (prn type-name "< Type")
  (prn field-name)
  (get-in {"QueryRoot" {"user" (fn [context parent args]
                                 {:name "test user name"
                                  :age 30})}}
          [type-name field-name]))

(defn roll-n-dice-n-times [sides times]
  (map (fn [_] (rand-int sides)) (range times)))

(defn handler-user [field-name]
  (fn [ctx p args]
    (prn "User contexts: " ctx p args)
    (get p (keyword field-name))))

(defn handler-query-dice [field-name]
  (case field-name
    "rollDice" (fn [c p a] (roll-n-dice-n-times (get a "numSides") (get a "numDice")))))

(defn handler-root [field-name]
  (case field-name
    ;; Basically chains the data downwards.
    "user" (fn [c p a] {:name "Matt" :age 36})
    (fn [c p a] p)))

(def type-to-handler-map
  {"QueryRoot" , handler-root
   "QueryDice" , handler-query-dice
   "User"      , handler-user})

;; It basically works down the query list, like a reduce call.
(defn resolver-fn-dispatcher [type-name field-name]
  (prn type-name field-name)
  (let [handler (get type-to-handler-map type-name)]
    (if handler
      (handler field-name)
      (fn [context parent args] nil))))

;; https://graphql.org/graphql-js/passing-arguments/
;; (def query-str (slurp "query.gql"))

;; (executor/execute nil schema-str resolver-fn query-str)

(defn test-dice []
  (let [schema (slurp "gql/schema/dice.gql")
        query (slurp "gql/query/dice.gql")]
    (executor/execute nil schema resolver-fn-dispatcher query)))